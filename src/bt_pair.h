#ifndef BT_PAIR_H
#define BT_PAIR_H

/*
 * 系统级蓝牙配对接口
 *
 * Linux  : 通过 D-Bus 调用 BlueZ（自动扫描 + Pair），无需手动 bluetoothctl
 * Windows: 不提供，返回 NULL/-1，界面应提示用户去系统设置完成配对
 */

/* 扫描经典蓝牙设备并返回 "名称\t地址\n..." 的 malloc 字符串(调用方 g_free/free)，
 * 无可用设备返回 NULL。timeout_ms<=0 时不扫描，直接返回已知设备。 */
char *bt_scan_devices(int timeout_ms);

/* 配对指定 MAC（内部会自动扫描发现）。成功 0，失败 -1。阻塞最长约 timeout_ms+手机确认时间 */
int bt_pair_device(const char *mac, int timeout_ms);

#endif /* BT_PAIR_H */
