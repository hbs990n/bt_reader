/*
 * bt_reader.c — 蓝牙 SPP 朗读客户端
 *
 * 连接手机端 "OfflineReader" SPP 服务 (RFCOMM)，以 UTF-8 文本行(行尾 \n)触发朗读。
 *
 * 用法:
 *   bt_reader <手机MAC> [文本]   命令行：连接、发送一行、断开
 *   bt_reader <手机MAC>          命令行交互模式
 *   bt_reader                    启动图形界面
 *
 * GUI:
 *   Linux   GTK3（低资源，系统包 libgtk-3-dev），内置 BlueZ 系统级扫描/配对
 *   Windows Win32 API（零额外依赖），配对请用系统设置
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "bt_link.h"
#include "bt_pair.h"

#define DEFAULT_CHANNEL 1
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

/* ============ 命令行模式（跨平台共用） ============ */

static int cli_main(int argc, char **argv)
{
    const char *mac = argv[1];
    int rc = 0;
    int fd;

    if (bt_init() != 0)
        return 1;

    fd = bt_open(mac, DEFAULT_CHANNEL);
    if (fd < 0) {
        bt_cleanup();
        fprintf(stderr, "连接失败\n");
        return 1;
    }
    fprintf(stderr, "已连接 %s\n", mac);

    if (argc >= 3) {
        size_t len = strlen(argv[2]);
        if (bt_send_line(fd, argv[2], len) != 0)
            rc = 1;
    } else {
        char line[BUF_SIZE];
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
    }

    bt_close(fd);
    bt_cleanup();
    fprintf(stderr, "已断开\n");
    return rc;
}

/* ============ Linux GUI (GTK3) ============ */
#ifndef _WIN32

#include <gtk/gtk.h>

static GtkWidget *win;
static GtkWidget *entry_mac;
static GtkWidget *combo_dev;
static GtkWidget *textview;
static GtkWidget *label_status;
static GtkWidget *btn_scan;
static GtkWidget *btn_pair;
static GtkWidget *btn_send;

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

    gtk_combo_box_text_remove_all(c);
    if (d->list) {
        char *save = NULL;
        char *line;
        for (line = strtok_r(d->list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!*line)
                continue;
            char *tab = strchr(line, '\t');
            const char *name = line;
            const char *addr = tab ? tab + 1 : line;
            if (tab)
                *tab = '\0';
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (%s)", *name ? name : addr, addr);
            gtk_combo_box_text_append_text(c, buf);
            n++;
        }
    }
    set_status("扫描完成，发现 %d 个设备", n);
    gtk_widget_set_sensitive(btn_scan, TRUE);
    gtk_widget_set_sensitive(btn_pair, TRUE);
    return G_SOURCE_REMOVE;
}

static gpointer scan_worker(gpointer data)
{
    (void)data;
    scan_data *d = g_new0(scan_data, 1);
    d->list = bt_scan_devices(8000);
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
    d->ok = bt_pair_device(d->addr, 8000);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, pair_done, d, pair_data_free);
    return NULL;
}

static void on_pair(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_dev));
    if (!sel) {
        set_status("请先「扫描设备」并选择要配对的设备");
        return;
    }
    char addr[64];
    extract_addr(sel, addr, sizeof(addr));
    g_free(sel);
    if (!*addr) {
        set_status("无法解析所选设备的 MAC");
        return;
    }
    pair_data *d = g_new0(pair_data, 1);
    strncpy(d->addr, addr, sizeof(d->addr) - 1);
    set_status("正在配对 %s，请在手机上确认...", addr);
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
    if (bt_init() == 0) {
        fd = bt_open(d->mac, DEFAULT_CHANNEL);
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

static void build_ui(void)
{
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "蓝牙朗读客户端");
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 400);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

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
    gtk_grid_attach(GTK_GRID(grid), btn_send, 0, 3, 5, 1);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_send), NULL);

    label_status = gtk_label_new("就绪");
    gtk_widget_set_halign(label_status, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_status, 0, 4, 5, 1);

    gtk_widget_show_all(win);
}

static int gui_gtk_main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    build_ui();
    gtk_main();
    return 0;
}

/* ============ Windows GUI (Win32) ============ */
#else

#include <windows.h>

#define IDC_MAC 101
#define IDC_TEXT 102
#define IDC_PAIR 103
#define IDC_SEND 104
#define WM_APP_DONE (WM_APP + 1)

static HWND hwnd;
static HWND hMac;
static HWND hText;
static HWND hBtnSend;
static HWND hStatus;

typedef struct {
    char mac[64];
    char *text;
    int rc;
} wsend;

static DWORD WINAPI wsend_worker(LPVOID p)
{
    wsend *d = p;
    int fd = -1;
    if (bt_init() == 0) {
        fd = bt_open(d->mac, DEFAULT_CHANNEL);
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
    PostMessageW(hwnd, WM_APP_DONE, (WPARAM)d, 0);
    return 0;
}

static void set_child_font(HWND child)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
}

static void on_send_click(void)
{
    wsend *d = calloc(1, sizeof(*d));
    wchar_t macw[64];
    int len;

    if (!d)
        return;
    GetWindowTextW(hMac, macw, 64);
    WideCharToMultiByte(CP_UTF8, 0, macw, -1, d->mac, sizeof(d->mac), NULL, NULL);

    len = GetWindowTextLengthW(hText);
    if (len > 0) {
        wchar_t *tw = malloc(((size_t)len + 1) * sizeof(wchar_t));
        int n;
        GetWindowTextW(hText, tw, len + 1);
        n = WideCharToMultiByte(CP_UTF8, 0, tw, -1, NULL, 0, NULL, NULL);
        d->text = malloc((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, tw, -1, d->text, n, NULL, NULL);
        free(tw);
    } else {
        d->text = calloc(1, 1);
    }

    if (!*d->mac || !*d->text) {
        free(d->text);
        free(d);
        MessageBoxW(hwnd, L"请填写手机 MAC 和要朗读的文本", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    SetWindowTextW(hStatus, L"正在连接并朗读...");
    EnableWindow(hBtnSend, FALSE);
    {
        HANDLE th = CreateThread(NULL, 0, wsend_worker, d, 0, NULL);
        if (th) {
            CloseHandle(th);
            return;
        }
    }
    free(d->text);
    free(d);
}

static void on_pair_click(void)
{
    MessageBoxW(hwnd,
                L"请在系统设置中配对手机：\n"
                L"设置 → 蓝牙和其他设备 → 添加设备\n"
                L"配对完成后回到本程序输入 MAC 即可。",
                L"配对", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        hMac = CreateWindowExW(0, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            90, 12, 280, 22, hw, (HMENU)IDC_MAC, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"手机MAC:",
            WS_CHILD | WS_VISIBLE, 12, 15, 74, 18, hw, NULL, NULL, NULL);
        hBtnSend = CreateWindowExW(0, L"BUTTON", L"连接并朗读",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            12, 44, 100, 26, hw, (HMENU)IDC_SEND, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"系统配对说明",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            122, 44, 110, 26, hw, (HMENU)IDC_PAIR, NULL, NULL);
        CreateWindowExW(0, L"STATIC", L"文本(每行一段):",
            WS_CHILD | WS_VISIBLE, 12, 82, 120, 18, hw, NULL, NULL, NULL);
        hText = CreateWindowExW(0, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_AUTOHSCROLL | WS_VSCROLL,
            12, 104, 490, 240, hw, (HMENU)IDC_TEXT, NULL, NULL);
        hStatus = CreateWindowExW(0, L"STATIC", L"就绪",
            WS_CHILD | WS_VISIBLE, 12, 356, 490, 20, hw, NULL, NULL, NULL);

        set_child_font(hMac);
        set_child_font(hText);
        set_child_font(hBtnSend);
        set_child_font(hStatus);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SEND:
            on_send_click();
            break;
        case IDC_PAIR:
            on_pair_click();
            break;
        }
        return 0;
    case WM_APP_DONE: {
        wsend *d = (wsend *)wp;
        const wchar_t *msg;
        if (d->rc == 0)
            msg = L"已发送，手机应开始朗读";
        else if (d->rc == -2)
            msg = L"连接失败：请确认手机已点「蓝牙朗读」且已配对";
        else
            msg = L"发送失败";
        SetWindowTextW(hStatus, msg);
        EnableWindow(hBtnSend, TRUE);
        free(d->text);
        free(d);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int gui_win_main(void)
{
    WNDCLASSW wc;
    MSG msg;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); /* IDC_ARROW */
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BtReaderWindow";
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, L"BtReaderWindow", L"蓝牙朗读客户端",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 430,
        NULL, NULL, wc.hInstance, NULL);
    if (!hwnd)
        return 1;

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

#endif /* _WIN32 */

int main(int argc, char **argv)
{
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }
    if (argc >= 2)
        return cli_main(argc, argv);
#ifndef _WIN32
    return gui_gtk_main(argc, argv);
#else
    return gui_win_main();
#endif
}
