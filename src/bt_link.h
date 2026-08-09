#ifndef BT_LINK_H
#define BT_LINK_H

#include <stddef.h>

/* 初始化蓝牙栈（Windows 需 WSAStartup）。成功 0，失败 -1 */
int bt_init(void);

/* 清理蓝牙栈 */
void bt_cleanup(void);

/* 连接手机 SPP 服务。Linux 通过 SDP 自动解析真实 RFCOMM 通道，
 * channel 仅在 SDP 查询失败时作为兜底；Windows 由系统按 SPP UUID 自动解析。
 * 成功返回连接句柄(fd)，失败 -1 */
int bt_open(const char *mac, int channel);

/* 发送一行文本（自动补 \n）。成功 0，失败 -1 */
int bt_send_line(int fd, const char *text, size_t len);

/* 关闭连接 */
void bt_close(int fd);

#endif /* BT_LINK_H */
