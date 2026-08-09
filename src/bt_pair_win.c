/*
 * bt_pair_win.c — Windows 不提供程序内配对，
 * 界面应提示用户去「设置 → 蓝牙」完成系统级配对。
 */

#include "bt_pair.h"

#include <stddef.h>

char *bt_scan_devices(int timeout_ms)
{
    (void)timeout_ms;
    return NULL;
}

int bt_pair_device(const char *mac, int timeout_ms)
{
    (void)mac;
    (void)timeout_ms;
    return -1;
}
