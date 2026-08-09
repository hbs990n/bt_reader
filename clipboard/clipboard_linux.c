#include "clipboard.h"

#include <string.h>

#include <gtk/gtk.h>

#include "bt_log.h"

/*
 * Linux 实现：依赖 GTK3（GUI 本身是 GTK，无额外系统依赖）。
 *
 * 实现方式：在 GTK 主循环上挂一个 400ms 定时器，用 gtk_clipboard_request_text
 * 异步读取剪切板（回调在 GTK 主线程执行），文本变化且非空才通知上层。
 * 启动时会先吸收当前内容作为基线，避免开关打开瞬间把已有内容重复发送。
 */

#define POLL_MS 400

static clipboard_on_text_t g_cb = NULL;
static void *g_userdata = NULL;
static guint g_timer = 0;
static char *g_last = NULL;
static int g_ready = 0;

static void on_got_text(GtkClipboard *clip, const char *text, gpointer data)
{
    (void)clip;
    (void)data;
    if (!text || !*text)
        return;
    if (!g_ready) {
        /* 首次读取只做基线，不触发回调，避免开启瞬间重发已有内容 */
        g_free(g_last);
        g_last = g_strdup(text);
        g_ready = 1;
        return;
    }
    if (g_last && strcmp(g_last, text) == 0)
        return;
    g_free(g_last);
    g_last = g_strdup(text);
    if (g_cb)
        g_cb(text, g_userdata);
}

static gboolean poll_cb(gpointer data)
{
    (void)data;
    gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               on_got_text, NULL);
    return G_SOURCE_CONTINUE;
}

int clipboard_start(void *hwnd, clipboard_on_text_t cb, void *userdata)
{
    (void)hwnd;
    clipboard_stop();
    g_cb = cb;
    g_userdata = userdata;
    g_ready = 0;
    g_timer = g_timeout_add(POLL_MS, poll_cb, NULL);
    bt_log("clipboard: Linux 监听启动");
    return 0;
}

void clipboard_stop(void)
{
    if (g_timer) {
        g_source_remove(g_timer);
        g_timer = 0;
    }
    g_cb = NULL;
    g_userdata = NULL;
    g_ready = 0;
    g_free(g_last);
    g_last = NULL;
    bt_log("clipboard: Linux 监听停止");
}

int clipboard_poll(void)
{
    return 0;
}
