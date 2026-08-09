/*
 * main.c — 蓝牙 SPP 朗读客户端入口
 *
 * 按功能分三个模块:
 *   bt/        蓝牙通信（扫描/配对/数据传输）
 *   ui/        界面 + 配置（GUI、配置持久化）
 *   clipboard/ 剪切板监听
 *
 * 用法:
 *   bt_reader <手机MAC> [文本]   命令行：连接、发送一行、断开
 *   bt_reader <手机MAC>          命令行交互模式
 *   bt_reader                    启动图形界面（含剪切板朗读开关）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bt.h"
#include "config.h"
#include "ui.h"
#include "bt_log.h"

#define BUF_SIZE 8192

static void usage(const char *prog)
{
    fprintf(stderr,
            "蓝牙 SPP 朗读客户端 (OfflineReader, UUID 00001101-0000-1000-8000-00805F9B34FB)\n"
            "用法:\n"
            "  %s <手机MAC> [文本]   连接、发送一行文本(自动补 \\n)、断开\n"
            "  %s <手机MAC>          交互模式，stdin 逐行发送，EOF(Ctrl+D/Z) 退出\n"
            "  %s                    启动图形界面\n"
            "示例:\n"
            "  %s AA:BB:CC:DD:EE:FF \"你好，请朗读\"\n",
            prog, prog, prog, prog);
}

static int cli_main(int argc, char **argv)
{
    const char *mac = argv[1];
    char line[BUF_SIZE];
    int fd;
    int rc = 0;

    if (argc >= 3) {
        rc = bt_send_text(mac, argv[2]) != 0;
        fprintf(stderr, rc ? "发送失败\n" : "已连接 %s 并发送，已断开\n", mac);
        return rc;
    }

    if (bt_init() != 0)
        return 1;
    bt_log("cli_main: 连接 %s", mac);
    fd = bt_open(mac, BT_DEFAULT_CHANNEL);
    if (fd < 0) {
        bt_cleanup();
        fprintf(stderr, "连接失败\n");
        return 1;
    }
    fprintf(stderr, "已连接 %s\n", mac);

    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;
        if (bt_send_line(fd, line, len) != 0) {
            rc = 1;
            break;
        }
    }

    bt_close(fd);
    bt_cleanup();
    fprintf(stderr, "已断开\n");
    return rc;
}

int main(int argc, char **argv)
{
    bt_install_crash_handler();
    bt_log("==== bt_reader 启动 ====");
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }
    if (argc >= 2)
        return cli_main(argc, argv);
    return ui_run_gui(argc, argv);
}
