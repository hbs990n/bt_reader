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
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
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

static int parse_mac_win(const char *str, BTH_ADDR *addr)
{
#ifdef _WIN32
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
#else
    (void)str;
    (void)addr;
    return -1;
#endif
}

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
    memset(&addr, 0, sizeof(addr));
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)channel;

    if (str2ba(mac, &addr.rc_bdaddr) < 0) {
        fprintf(stderr, "MAC 格式错误: %s (应为 AA:BB:CC:DD:EE:FF)\n", mac);
        return -1;
    }

    int s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (s < 0) {
        print_sock_err("socket(AF_BLUETOOTH) 失败(请安装 libbluetooth-dev)");
        return -1;
    }
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        print_sock_err("连接失败(请确认手机已开启「蓝牙朗读」并已配对)");
        close(s);
        return -1;
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

void bt_close(int fd)
{
    if (fd >= 0)
        CLOSE_SOCK((SOCKET)fd);
}
