/*
 * ui_gtk.c — Linux GTK3 图形界面
 *
 * 功能:
 *   1. 扫描 / 配对 / 手动输入 MAC
 *   2. 输入文本 → 连接手机并朗读
 *   3. 「剪切板朗读」开关：开启后监听本机剪切板，文本变化自动发送朗读
 *   4. MAC 与剪切板开关状态持久化到配置文件
 *
 * 蓝牙扫描/配对用 BlueZ D-Bus（bt 模块），连接发送用 RFCOMM（bt 模块）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <gtk/gtk.h>

#include "bt.h"
#include "config.h"
#include "clipboard.h"
#include "bt_log.h"

static GtkWidget *win;
static GtkWidget *entry_mac;
static GtkWidget *combo_dev;
static GtkWidget *textview;
static GtkWidget *label_status;
static GtkWidget *btn_scan;
static GtkWidget *btn_pair;
static GtkWidget *btn_send;
static GtkWidget *btn_clip;

static app_config g_cfg;
static char g_clip_mac[64];  /* 开启剪切板朗读时锁定的 MAC */
static int  g_clip_busy;     /* 上一文本尚未发送完时丢弃新文本 */

static void set_status(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(label_status), buf);
}

static void extract_addr(const char *text, char *out, size_t outsz)
{
    const char *lp = strrchr(text, '(');
    out[0] = '\0';
    if (!lp)
        return;
    const char *rp = strrchr(lp, ')');
    if (!rp)
        return;
    size_t n = (size_t)(rp - lp - 1);
    if (n >= outsz)
        n = outsz - 1;
    memcpy(out, lp + 1, n);
    out[n] = '\0';
}

/* ---- 扫描 ---- */

typedef struct {
    char *list;
} scan_data;

static void scan_data_free(gpointer p)
{
    scan_data *d = p;
    g_free(d->list);
    g_free(d);
}

static gboolean scan_done(gpointer p)
{
    scan_data *d = p;
    GtkComboBoxText *c = GTK_COMBO_BOX_TEXT(combo_dev);
    int n = 0;
    GString *out = g_string_new(NULL);

    gtk_combo_box_text_remove_all(c);
    if (d->list) {
        char *save = NULL;
        char *line;
        for (line = strtok_r(d->list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line)
                continue;
            char *tab1 = strchr(line, '\t');
            char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
            const char *name = line;
            const char *addr = tab1 ? tab1 + 1 : line;
            if (tab1)
                *tab1 = '\0';
            if (tab2)
                *tab2 = '\0';
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (%s)", *name ? name : addr, addr);
            gtk_combo_box_text_append_text(c, buf);
            g_string_append_printf(out, "%s\t%s\n", addr, *name ? name : addr);
            n++;
        }
    }
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    if (n > 0)
        gtk_text_buffer_set_text(buf, out->str, -1);
    else
        gtk_text_buffer_set_text(buf, "（未发现设备）", -1);
    g_string_free(out, TRUE);

    set_status("扫描完成，发现 %d 个设备（下方文本框可复制）", n);
    bt_log("scan_done: 发现 %d 个设备", n);
    gtk_widget_set_sensitive(btn_scan, TRUE);
    gtk_widget_set_sensitive(btn_pair, TRUE);
    return G_SOURCE_REMOVE;
}

static gpointer scan_worker(gpointer data)
{
    (void)data;
    scan_data *d = g_new0(scan_data, 1);
    bt_log("scan_worker: 开始扫描");
    d->list = bt_scan_devices(8000);
    bt_log("scan_worker: 扫描返回 %s", d->list ? "有结果" : "空/NULL");
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, scan_done, d, scan_data_free);
    return NULL;
}

static void on_scan(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    set_status("正在扫描...");
    gtk_widget_set_sensitive(btn_scan, FALSE);
    gtk_widget_set_sensitive(btn_pair, FALSE);
    GThread *t = g_thread_new("scan", scan_worker, NULL);
    g_thread_unref(t);
}

/* ---- 配对 ---- */

typedef struct {
    char addr[64];
    int ok;
} pair_data;

static void pair_data_free(gpointer p)
{
    g_free(p);
}

static gboolean pair_done(gpointer p)
{
    pair_data *d = p;
    if (d->ok == 0)
        set_status("配对成功");
    else
        set_status("配对失败（请确认手机弹出请求并点「配对」）");
    gtk_widget_set_sensitive(btn_pair, TRUE);
    gtk_widget_set_sensitive(btn_scan, TRUE);
    return G_SOURCE_REMOVE;
}

static gpointer pair_worker(gpointer data)
{
    pair_data *d = data;
    bt_log("pair_worker: 配对 %s", d->addr);
    d->ok = bt_pair_device(d->addr, 8000);
    bt_log("pair_worker: 配对结果 %d", d->ok);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, pair_done, d, pair_data_free);
    return NULL;
}

static void on_pair(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    char addr[64] = {0};
    char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_dev));
    if (sel) {
        extract_addr(sel, addr, sizeof(addr));
        g_free(sel);
    }
    if (!*addr) {
        const char *mac = gtk_entry_get_text(GTK_ENTRY(entry_mac));
        strncpy(addr, mac, sizeof(addr) - 1);
        addr[sizeof(addr) - 1] = '\0';
    }
    if (!*addr) {
        set_status("请填写手机 MAC（或先扫描并选择设备）");
        return;
    }
    pair_data *d = g_new0(pair_data, 1);
    strncpy(d->addr, addr, sizeof(d->addr) - 1);
    set_status("正在配对 %s，请在手机上确认...", addr);
    bt_log("on_pair: 发起配对 %s", addr);
    gtk_widget_set_sensitive(btn_pair, FALSE);
    gtk_widget_set_sensitive(btn_scan, FALSE);
    GThread *t = g_thread_new("pair", pair_worker, d);
    g_thread_unref(t);
}

/* ---- 连接并朗读 ---- */

typedef struct {
    char mac[64];
    char *text;
    int rc;
} send_data;

static void send_data_free(gpointer p)
{
    send_data *d = p;
    g_free(d->text);
    g_free(d);
}

static gboolean send_done(gpointer p)
{
    send_data *d = p;
    if (d->rc == 0)
        set_status("已发送，手机应开始朗读");
    else if (d->rc == -2)
        set_status("连接失败：请确认手机已点「蓝牙朗读」且已配对");
    else
        set_status("发送失败");
    gtk_widget_set_sensitive(btn_send, TRUE);
    return G_SOURCE_REMOVE;
}

static gpointer send_worker(gpointer data)
{
    send_data *d = data;
    int fd = -1;
    bt_log("send_worker: 连接 %s，文本 %zu 字节", d->mac, strlen(d->text));
    if (bt_init() == 0) {
        fd = bt_open(d->mac, BT_DEFAULT_CHANNEL);
        if (fd >= 0) {
            d->rc = bt_send_line(fd, d->text, strlen(d->text));
            bt_close(fd);
        } else {
            d->rc = -2;
        }
        bt_cleanup();
    } else {
        d->rc = -3;
    }
    bt_log("send_worker: 结果 rc=%d", d->rc);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, send_done, d, send_data_free);
    return NULL;
}

static void on_send(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    const char *mac = gtk_entry_get_text(GTK_ENTRY(entry_mac));
    if (!*mac) {
        set_status("请先填写手机 MAC");
        return;
    }
    strncpy(g_cfg.mac, mac, sizeof(g_cfg.mac) - 1);
    g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    char *txt = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
    if (!*txt) {
        g_free(txt);
        set_status("请先输入要朗读的文本");
        return;
    }
    send_data *d = g_new0(send_data, 1);
    strncpy(d->mac, mac, sizeof(d->mac) - 1);
    d->text = g_strdup(txt);
    g_free(txt);
    set_status("正在连接 %s ...", mac);
    gtk_widget_set_sensitive(btn_send, FALSE);
    GThread *t = g_thread_new("send", send_worker, d);
    g_thread_unref(t);
}

/* ---- 剪切板朗读 ---- */

typedef struct {
    char mac[64];
    char *text;
    int rc;
} clip_send_data;

static gboolean clip_done(gpointer p)
{
    clip_send_data *d = p;
    if (d->rc == 0)
        set_status("剪切板已发送，手机应开始朗读");
    else
        set_status("剪切板发送失败（请确认手机已开启「蓝牙朗读」且已配对）");
    bt_log("clipboard: 发送结果 rc=%d", d->rc);
    g_free(d->text);
    g_free(d);
    g_clip_busy = 0;
    return G_SOURCE_REMOVE;
}

static gpointer clip_worker(gpointer data)
{
    clip_send_data *d = data;
    bt_log("clipboard: 连接 %s，文本 %zu 字节", d->mac, strlen(d->text));
    d->rc = bt_send_text(d->mac, d->text);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, clip_done, d, NULL);
    return NULL;
}

/* 在主线程触发：开工作线程做连接+发送，避免阻塞 GUI */
static void clip_send_text(const char *text)
{
    clip_send_data *d = g_new0(clip_send_data, 1);
    strncpy(d->mac, g_clip_mac, sizeof(d->mac) - 1);
    d->mac[sizeof(d->mac) - 1] = '\0';
    d->text = g_strdup(text);
    set_status("剪切板变化，正在连接手机...");
    GThread *t = g_thread_new("clip", clip_worker, d);
    g_thread_unref(t);
}

static void on_clip_text(const char *text, void *userdata)
{
    (void)userdata;
    if (g_clip_busy) {
        bt_log("clipboard: 上一文本仍在发送，跳过本次");
        return;
    }
    if (!*g_clip_mac) {
        bt_log("clipboard: 无有效 MAC，忽略");
        return;
    }
    g_clip_busy = 1;
    clip_send_text(text);
}

static void on_clip_toggled(GtkToggleButton *b, gpointer data)
{
    (void)data;
    if (gtk_toggle_button_get_active(b)) {
        const char *mac = gtk_entry_get_text(GTK_ENTRY(entry_mac));
        if (!*mac) {
            gtk_toggle_button_set_active(b, FALSE);
            set_status("请先填写手机 MAC 再开启剪切板朗读");
            return;
        }
        strncpy(g_cfg.mac, mac, sizeof(g_cfg.mac) - 1);
        g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';
        strncpy(g_clip_mac, mac, sizeof(g_clip_mac) - 1);
        g_clip_mac[sizeof(g_clip_mac) - 1] = '\0';
        if (clipboard_start(NULL, on_clip_text, NULL) == 0) {
            g_cfg.clipboard_enabled = 1;
            config_save(&g_cfg);
            set_status("剪切板朗读已开启（复制文本即自动朗读）");
        } else {
            gtk_toggle_button_set_active(b, FALSE);
            set_status("剪切板监听启动失败");
        }
    } else {
        clipboard_stop();
        g_cfg.clipboard_enabled = 0;
        config_save(&g_cfg);
        set_status("剪切板朗读已关闭");
    }
}

/* ---- 界面 ---- */

static void on_combo_changed(GtkComboBox *cb, gpointer data)
{
    (void)data;
    char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cb));
    if (sel) {
        char addr[64];
        extract_addr(sel, addr, sizeof(addr));
        if (*addr)
            gtk_entry_set_text(GTK_ENTRY(entry_mac), addr);
        g_free(sel);
    }
}

static void on_win_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    clipboard_stop();
    strncpy(g_cfg.mac, gtk_entry_get_text(GTK_ENTRY(entry_mac)), sizeof(g_cfg.mac) - 1);
    g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';
    config_save(&g_cfg);
    gtk_main_quit();
}

static void build_ui(void)
{
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "蓝牙朗读客户端");
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 400);
    g_signal_connect(win, "destroy", G_CALLBACK(on_win_destroy), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(win), grid);

    GtkWidget *lbl_mac = gtk_label_new("手机MAC:");
    gtk_widget_set_halign(lbl_mac, GTK_ALIGN_END);
    entry_mac = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_mac), "AA:BB:CC:DD:EE:FF");
    gtk_widget_set_hexpand(entry_mac, TRUE);
    gtk_grid_attach(GTK_GRID(grid), lbl_mac, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_mac, 1, 0, 2, 1);

    btn_scan = gtk_button_new_with_label("扫描设备");
    btn_pair = gtk_button_new_with_label("配对");
    gtk_grid_attach(GTK_GRID(grid), btn_scan, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_pair, 4, 0, 1, 1);
    g_signal_connect(btn_scan, "clicked", G_CALLBACK(on_scan), NULL);
    g_signal_connect(btn_pair, "clicked", G_CALLBACK(on_pair), NULL);

    combo_dev = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(combo_dev, TRUE);
    gtk_grid_attach(GTK_GRID(grid), combo_dev, 0, 1, 5, 1);
    g_signal_connect(combo_dev, "changed", G_CALLBACK(on_combo_changed), NULL);

    GtkWidget *lbl_txt = gtk_label_new("文本:");
    gtk_widget_set_halign(lbl_txt, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), lbl_txt, 0, 2, 1, 1);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(sc, TRUE);
    gtk_widget_set_vexpand(sc, TRUE);
    textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(sc), textview);
    gtk_grid_attach(GTK_GRID(grid), sc, 1, 2, 4, 1);

    btn_send = gtk_button_new_with_label("连接并朗读");
    gtk_grid_attach(GTK_GRID(grid), btn_send, 0, 3, 3, 1);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_send), NULL);

    btn_clip = gtk_check_button_new_with_label("剪切板朗读");
    gtk_grid_attach(GTK_GRID(grid), btn_clip, 3, 3, 2, 1);
    g_signal_connect(btn_clip, "toggled", G_CALLBACK(on_clip_toggled), NULL);

    label_status = gtk_label_new("就绪");
    gtk_widget_set_halign(label_status, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_status, 0, 4, 5, 1);

    gtk_widget_show_all(win);
}

int ui_run_gui(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    config_load(&g_cfg);
    build_ui();

    if (*g_cfg.mac)
        gtk_entry_set_text(GTK_ENTRY(entry_mac), g_cfg.mac);
    if (g_cfg.clipboard_enabled)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_clip), TRUE);

    gtk_main();
    return 0;
}
