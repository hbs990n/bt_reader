#ifndef BT_MODULE_H
#define BT_MODULE_H

/*
 * bt 模块 — 蓝牙通信统一入口
 *
 * 包含两个子层:
 *   bt_link.h  连接 / 数据传输 (RFCOMM SPP)
 *   bt_pair.h  系统级扫描 / 配对 (BlueZ D-Bus)
 *
 * 其他模块统一 #include "bt.h"，不要直接依赖子层头。
 */

#include "bt_link.h"
#include "bt_pair.h"

#endif /* BT_MODULE_H */
