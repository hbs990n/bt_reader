/*
 * bt_link.c — 蓝牙 RFCOMM (SPP) 连接与发送
 *
 * Linux   BlueZ  : 需 libbluetooth-dev，链接 -lbluetooth
 * Windows Winsock: 链接 ws2_32 + Bthprops.lib
 */

#include "bt_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2bth.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")
#define CLOSE_SOCK closesocket
#define BT_OK_SOCK(s) ((s) != INVALID_SOCKET)
#else
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#define CLOSE_SOCK close
#define SOCKET int
#define INVALID_SOCKET (-1)
#define BT_OK_SOCK(s) ((s) >= 0)
#endif

/* Windows SPP 服务 UUID: 00001101-0000-1000-8000-00805F9B34FB */
#ifdef _WIN32
static const GUID SPP_SVC_UUID = { 0x00001101, 0x0000, 0x1000,
                                   { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
#ifndef BTH_PORT_ANY
#define BTH_PORT_ANY 0
#endif
#else
#define CONNECT_TIMEOUT_MS 8000
/* 从 SDP 服务记录里提取 RFCOMM 通道号 */
static int extract_rfcomm_channel(sdp_record_t *rec)
{
    sdp_list_t *protos = NULL;
    int channel = -1;

    if (sdp_get_access_protos(rec, &protos) == 0) {
        int port = sdp_get_proto_port(protos, RFCOMM_UUID);
        if (port > 0)
            channel = port;
        sdp_list_free(protos, 0);
    }
    return channel;
}

/* SDP 查询对方 SPP (Serial Port) 服务，返回真实 RFCOMM 通道号；失败 -1 */
static int resolve_spp_channel(const bdaddr_t *dst)
{
    sdp_session_t *session = sdp_connect(BDADDR_ANY, dst, SDP_RETRY_IF_BUSY);
    uuid_t svc;
    sdp_list_t *search, *attrs, *recs = NULL;
    uint32_t range = 0x0000ffff;
    int channel = -1;

    if (!session)
        return -1;
    sdp_uuid16_create(&svc, SERIAL_PORT_SVCLASS_ID);
    search = sdp_list_append(NULL, &svc);
    attrs = sdp_list_append(NULL, &range);
    if (sdp_service_search_attr_req(session, search, SDP_ATTR_REQ_RANGE, attrs, &recs) == 0) {
        for (sdp_list_t *r = recs; r; r = r->next) {
            sdp_record_t *rec = (sdp_record_t *)r->data;
            int ch = extract_rfcomm_channel(rec);
            if (ch > 0) {
                channel = ch;
                break;
            }
        }
        for (sdp_list_t *r = recs; r; r = r->next)
            sdp_record_free((sdp_record_t *)r->data);
        sdp_list_free(recs, 0);
    }
    sdp_list_free(search, 0);
    sdp_list_free(attrs, 0);
    sdp_close(session);
    return channel;
}
#endif

static int g_initialized = 0;

static void print_sock_err(const char *what)
{
#ifdef _WIN32
    fprintf(stderr, "%s: 错误 %lu\n", what, (unsigned long)WSAGetLastError());
#else
    fprintf(stderr, "%s: %s\n", what, strerror(errno));
#endif
}

int bt_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (g_initialized)
        return 0;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup 失败: %lu\n", (unsigned long)WSAGetLastError());
        return -1;
    }
    g_initialized = 1;
#else
    g_initialized = 1;
#endif
    return 0;
}

void bt_cleanup(void)
{
#ifdef _WIN32
    if (g_initialized) {
        WSACleanup();
        g_initialized = 0;
    }
#else
    g_initialized = 0;
#endif
}

#ifdef _WIN32
static int parse_mac_win(const char *str, BTH_ADDR *addr)
{
    unsigned int b[6];
    int n = sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    if (n != 6)
        return -1;
    /* BTH_ADDR 为 64 位，字节在内存中小端排列: FF EE DD CC BB AA 00 00 */
    *addr = ((BTH_ADDR)b[0] << 40) | ((BTH_ADDR)b[1] << 32) |
            ((BTH_ADDR)b[2] << 24) | ((BTH_ADDR)b[3] << 16) |
            ((BTH_ADDR)b[4] << 8)  | (BTH_ADDR)b[5];
    return 0;
}
#endif

int bt_open(const char *mac, int channel)
{
#ifdef _WIN32
    (void)channel;
    SOCKADDR_BTH addr;
    memset(&addr, 0, sizeof(addr));
    addr.addressFamily = AF_BTH;
    addr.btAddr = 0;
    addr.serviceClassId = SPP_SVC_UUID; /* SDP 查询 SPP UUID 自动解析 */
    addr.port = BTH_PORT_ANY;

    if (parse_mac_win(mac, &addr.btAddr) < 0) {
        fprintf(stderr, "MAC 格式错误: %s (应为 AA:BB:CC:DD:EE:FF)\n", mac);
        return -1;
    }

    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (!BT_OK_SOCK(s)) {
        print_sock_err("socket(AF_BTH) 失败");
        return -1;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        print_sock_err("连接失败(请确认手机已开启「蓝牙朗读」并已配对)");
        closesocket(s);
        return -1;
    }
    return (int)s;
#else
    struct sockaddr_rc addr;
    bdaddr_t dst;
    int s;
    int chan = -1;

    if (str2ba(mac, &dst) < 0) {
        fprintf(stderr, "MAC 格式错误: %s (应为 AA:BB:CC:DD:EE:FF)\n", mac);
        return -1;
    }

    /* 优先通过 SDP 解析 SPP 服务的真实 RFCOMM 通道（Android 分配的不一定是 1） */
    chan = resolve_spp_channel(&dst);
    if (chan < 0)
        chan = (uint8_t)channel; /* SDP 不可用时退回默认通道 */
    fprintf(stderr, "SPP 通道: %d\n", chan);

    memset(&addr, 0, sizeof(addr));
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)chan;
    addr.rc_bdaddr = dst;

    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (s < 0) {
        print_sock_err("socket(AF_BLUETOOTH) 失败(请安装 libbluetooth-dev)");
        return -1;
    }
    /* 非阻塞 + poll，限制连接超时，避免失败时阻塞几十秒 */
    {
        int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0)
            flags = 0;
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno == EINPROGRESS || errno == EAGAIN) {
                struct pollfd pfd;
                int r;
                pfd.fd = s;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                r = poll(&pfd, 1, CONNECT_TIMEOUT_MS);
                if (r > 0) {
                    int soerr = 0;
                    socklen_t elen = sizeof(soerr);
                    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &elen) == 0 && soerr == 0) {
                        /* 连接成功 */
                    } else {
                        print_sock_err("连接失败(请确认手机已开启「蓝牙朗读」并已配对)");
                        fcntl(s, F_SETFL, flags);
                        close(s);
                        return -1;
                    }
                } else {
                    fprintf(stderr, "连接超时(%d 秒)，请确认手机已开启「蓝牙朗读」且 SPP 服务在监听\n",
                            CONNECT_TIMEOUT_MS / 1000);
                    fcntl(s, F_SETFL, flags);
                    close(s);
                    return -1;
                }
            } else {
                print_sock_err("连接失败(请确认手机已开启「蓝牙朗读」并已配对)");
                fcntl(s, F_SETFL, flags);
                close(s);
                return -1;
            }
        }
        fcntl(s, F_SETFL, flags);
    }
    return s;
#endif
}

static int send_all(int fd, const char *buf, int len)
{
    SOCKET s = (SOCKET)fd;
    int off = 0;
    while (off < len) {
        int flags = 0;
#ifdef _WIN32
        int n = send(s, buf + off, len - off, flags);
#else
        flags = MSG_NOSIGNAL; /* 避免连接断开时 SIGPIPE 崩溃 */
        int n = (int)send(s, buf + off, (size_t)(len - off), flags);
#endif
        if (n <= 0) {
            print_sock_err("发送失败");
            return -1;
        }
        off += n;
    }
    return 0;
}

int bt_send_line(int fd, const char *text, size_t len)
{
    char *msg = (char *)malloc(len + 1);
    int rc;
    if (!msg)
        return -1;
    memcpy(msg, text, len);
    msg[len] = '\n';
    rc = send_all(fd, msg, (int)(len + 1));
    free(msg);
    return rc;
}

int bt_send_text(const char *mac, const char *text)
{
    int fd;
    int rc = -1;

    if (!mac || !text || bt_init() != 0)
        return -1;
    fd = bt_open(mac, BT_DEFAULT_CHANNEL);
    if (fd >= 0) {
        rc = bt_send_line(fd, text, strlen(text));
        bt_close(fd);
    }
    bt_cleanup();
    return rc;
}

void bt_close(int fd)
{
    if (fd >= 0)
        CLOSE_SOCK((SOCKET)fd);
}
