/*
 * bt_pair_linux.c — Linux BlueZ 系统级扫描/配对（D-Bus）
 *
 * 通过系统总线调用 org.bluez:
 *   Adapter1.StartDiscovery / StopDiscovery
 *   Device1.Pair
 *   ObjectManager.GetManagedObjects 收集设备
 */

#define _GNU_SOURCE

#include "bt_pair.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <gio/gio.h>

#include "bt_log.h"

#define BT_BUS      "org.bluez"
#define BT_ADAPTER  "/org/bluez/hci0"
#define BT_OM       "org.freedesktop.DBus.ObjectManager"
#define BT_PROPS    "org.freedesktop.DBus.Properties"

static GDBusConnection *get_conn(void)
{
    GError *err = NULL;
    GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!c) {
        bt_log("无法连接系统 D-Bus 总线: %s", err ? err->message : "?");
        fprintf(stderr, "无法连接系统 D-Bus 总线: %s\n", err ? err->message : "?");
        g_clear_error(&err);
    }
    return c;
}

static int set_powered(GDBusConnection *c, gboolean on)
{
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(c, BT_BUS, BT_ADAPTER, BT_PROPS, "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(on)),
        G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (!res) {
        bt_log("设置 Powered=%d 失败: %s", on, err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    g_variant_unref(res);
    return 0;
}

static int adapter_call(GDBusConnection *c, const char *method)
{
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(c, BT_BUS, BT_ADAPTER, "org.bluez.Adapter1",
        method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (!res) {
        bt_log("adapter_call(%s) 失败: %s", method, err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    g_variant_unref(res);
    bt_log("adapter_call(%s) 成功", method);
    return 0;
}

static void wait_ms(int ms)
{
    gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
    while (g_get_monotonic_time() < end)
        g_usleep(100000);
}

/* 收集所有 Device1，返回 "名称\t地址\t对象路径\n..."，调用方 g_free */
static char *collect_devices(GDBusConnection *c)
{
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(c, BT_BUS, "/", BT_OM, "GetManagedObjects",
        NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &err);
    if (!res) {
        g_clear_error(&err);
        return NULL;
    }

    GString *out = g_string_new(NULL);
    GVariant *root = g_variant_get_child_value(res, 0);
    gsize n = g_variant_n_children(root);
    bt_log("collect_devices: GetManagedObjects 返回 %zu 个对象", n);
    int count = 0;

    for (gsize i = 0; i < n; i++) {
        GVariant *obj = g_variant_get_child_value(root, i);   /* {oa{sa{sv}}} */
        GVariant *pv = g_variant_get_child_value(obj, 0);     /* o */
        GVariant *ifaces = g_variant_get_child_value(obj, 1); /* a{sa{sv}} */
        const char *path = g_variant_get_string(pv, NULL);
        GVariant *dev = g_variant_lookup_value(ifaces, "org.bluez.Device1",
                                               G_VARIANT_TYPE("a{sv}"));
        const char *addr = NULL;
        const char *name = NULL;
        if (dev) {
            g_variant_lookup(dev, "Address", "s", &addr);
            g_variant_lookup(dev, "Name", "s", &name);
            if (addr) {
                g_string_append_printf(out, "%s\t%s\t%s\n", name ? name : "", addr, path);
                count++;
            }
            g_variant_unref(dev);
        }
        g_variant_unref(ifaces);
        g_variant_unref(pv);
        g_variant_unref(obj);
    }
    bt_log("collect_devices: 共 %d 个蓝牙设备", count);

    g_variant_unref(root);
    g_variant_unref(res);
    return g_string_free(out, FALSE);
}

static char *find_device_path(const char *list, const char *mac)
{
    if (!list || !mac)
        return NULL;
    char *copy = g_strdup(list);
    char *save = NULL;
    char *path = NULL;
    char *line;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!*line)
            continue;
        char *t1 = strchr(line, '\t');
        if (!t1)
            continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2)
            continue;
        *t2 = '\0';
        if (strcasecmp(t1 + 1, mac) == 0) {
            path = g_strdup(t2 + 1);
            break;
        }
    }
    g_free(copy);
    return path;
}

char *bt_scan_devices(int timeout_ms)
{
    GDBusConnection *c = get_conn();
    char *list = NULL;
    if (!c)
        return NULL;
    if (set_powered(c, TRUE) < 0) {
        g_object_unref(c);
        return NULL;
    }
    if (adapter_call(c, "StartDiscovery") < 0) {
        g_object_unref(c);
        return NULL;
    }
    bt_log("扫描开始，时长 %d ms", timeout_ms);
    if (timeout_ms > 0)
        wait_ms(timeout_ms);
    adapter_call(c, "StopDiscovery");
    list = collect_devices(c);
    bt_log("扫描结束，设备列表长度 %zu", list ? strlen(list) : 0);
    g_object_unref(c);
    return list;
}

int bt_pair_device(const char *mac, int timeout_ms)
{
    GDBusConnection *c = get_conn();
    char *list;
    char *path;
    int rc = -1;

    bt_log("配对请求: %s", mac);
    if (!c)
        return -1;
    if (set_powered(c, TRUE) < 0) {
        g_object_unref(c);
        return -1;
    }
    if (adapter_call(c, "StartDiscovery") < 0) {
        g_object_unref(c);
        return -1;
    }
    if (timeout_ms > 0)
        wait_ms(timeout_ms);
    adapter_call(c, "StopDiscovery");

    list = collect_devices(c);
    path = find_device_path(list, mac);
    g_free(list);

    if (!path) {
        bt_log("未在扫描结果中找到 %s", mac);
        g_object_unref(c);
        return -1;
    }
    bt_log("找到设备对象路径: %s", path);

    {
        GError *err = NULL;
        GVariant *res = g_dbus_connection_call_sync(c, BT_BUS, path, "org.bluez.Device1", "Pair",
            NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 90000, NULL, &err);
        g_free(path);
        if (!res) {
            bt_log("Pair 失败: %s", err ? err->message : "?");
            g_clear_error(&err);
        } else {
            bt_log("Pair 成功");
            g_variant_unref(res);
            rc = 0;
        }
    }
    g_object_unref(c);
    return rc;
}
