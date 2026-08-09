/*
 * bt_reader — 蓝牙 SPP 朗读客户端
 *
 * 连接手机端 "OfflineReader" SPP 服务 (RFCOMM)，以 UTF-8 文本行
 * (行尾 \n) 触发手机离线 TTS 朗读。
 *
 * 用法:
 *   bt_reader <手机MAC> [文本]   连接、发送一行文本(自动补 \n)、断开
 *   bt_reader <手机MAC>          交互模式，从 stdin 逐行发送，EOF 退出
 *
 * 平台:
 *   Linux   BlueZ  : 需 libbluetooth-dev，链接 -lbluetooth
 *   Windows Winsock: 链接 ws2_32 + Bthprops.lib
 */

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
#else
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#define CLOSE_SOCK close
#define SOCKET int
#define INVALID_SOCKET (-1)
#endif

#define SPP_UUID_STR "00001101-0000-1000-8000-00805F9B34FB"
#define DEFAULT_CHANNEL 1
#define BUF_SIZE 8192

/* ============ 平台初始/清理 ============ */

#ifdef _WIN32
static WSADATA g_wsa;
static int bluetooth_init(void)
{
    if (WSAStartup(MAKEWORD(2, 2), &g_wsa) != 0) {
        fprintf(stderr, "WSAStartup 失败: %lu\n", (unsigned long)WSAGetLastError());
        return -1;
    }
    return 0;
}
static void bluetooth_cleanup(void)
{
    WSACleanup();
}
static void print_sock_err(const char *what)
{
    fprintf(stderr, "%s: 错误 %lu\n", what, (unsigned long)WSAGetLastError());
}
#else
static int bluetooth_init(void) { return 0; }
static void bluetooth_cleanup(void) {}
static void print_sock_err(const char *what)
{
    fprintf(stderr, "%s: %s\n", what, strerror(errno));
}
#endif

/* ============ MAC 解析 ============ */

#ifdef _WIN32
/* SPP 服务 UUID: 00001101-0000-1000-8000-00805F9B34FB */
static const GUID SPP_SVC_UUID = { 0x00001101, 0x0000, 0x1000,
                                   { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
#ifndef BTH_PORT_ANY
#define BTH_PORT_ANY 0
#endif

static int parse_mac(const char *str, BTH_ADDR *addr)
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

/* ============ 连接 ============ */

static int open_conn(const char *mac_str, int channel)
{
#ifdef _WIN32
    (void)channel;
    SOCKADDR_BTH addr;
    memset(&addr, 0, sizeof(addr));
    addr.addressFamily = AF_BTH;
    addr.btAddr = 0;
    addr.serviceClassId = SPP_SVC_UUID; /* SDP 查询 SPP UUID 自动解析 */
    addr.port = BTH_PORT_ANY;

    if (parse_mac(mac_str, &addr.btAddr) < 0) {
        fprintf(stderr, "MAC 格式错误: %s (应为 AA:BB:CC:DD:EE:FF)\n", mac_str);
        return -1;
    }

    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) {
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

    if (str2ba(mac_str, &addr.rc_bdaddr) < 0) {
        fprintf(stderr, "MAC 格式错误: %s (应为 AA:BB:CC:DD:EE:FF)\n", mac_str);
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

/* ============ 发送 ============ */

static int send_all(SOCKET s, const char *buf, int len)
{
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

static int send_line(SOCKET s, const char *text, size_t len)
{
    char *msg = (char *)malloc(len + 1);
    int rc;
    if (!msg)
        return -1;
    memcpy(msg, text, len);
    msg[len] = '\n';
    rc = send_all(s, msg, (int)(len + 1));
    free(msg);
    return rc;
}

/* ============ 帮助 ============ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "蓝牙 SPP 朗读客户端 (OfflineReader, UUID %s)\n"
            "用法:\n"
            "  %s <手机MAC> [文本]   连接、发送一行文本(自动补 \\n)、断开\n"
            "  %s <手机MAC>          交互模式，stdin 逐行发送，EOF(Ctrl+D/Z) 退出\n"
            "示例:\n"
            "  %s AA:BB:CC:DD:EE:FF \"你好，请朗读\"\n",
            SPP_UUID_STR, prog, prog, prog);
}

/* ============ 主程序 ============ */

int main(int argc, char **argv)
{
    const char *mac;
    int rc = 0;
    SOCKET s;
    char line[BUF_SIZE];

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    mac = argv[1];

    if (bluetooth_init() != 0)
        return 1;

    s = open_conn(mac, DEFAULT_CHANNEL);
    if (s == INVALID_SOCKET) {
        bluetooth_cleanup();
        return 1;
    }
    fprintf(stderr, "已连接 %s\n", mac);

    if (argc >= 3) {
        /* 单次模式: 发送一行后断开 */
        size_t len = strlen(argv[2]);
        if (send_line(s, argv[2], len) != 0)
            rc = 1;
    } else {
        /* 交互模式 */
        while (fgets(line, sizeof(line), stdin)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len == 0)
                continue;
            if (send_line(s, line, len) != 0) {
                rc = 1;
                break;
            }
        }
    }

    CLOSE_SOCK(s);
    bluetooth_cleanup();
    fprintf(stderr, "已断开\n");
    return rc;
}
