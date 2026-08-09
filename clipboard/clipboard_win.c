#include "clipboard.h"

#include <string.h>
#include <stdlib.h>

#include <windows.h>

#include "bt_log.h"

/*
 * Windows 实现：剪贴板格式监听（AddClipboardFormatListener，剪切板一变立即
 * 收到 WM_CLIPBOARDUPDATE）+ 1.5s 定时轮询兜底（某些剪贴板提供方不广播通知）。
 * 两者都汇入 clipboard_poll()，在 GUI 线程执行，由 ui_win.c 从消息循环调用。
 */

#define POLL_TIMER_ID 1
#define POLL_MS 1500
#define CLIP_BUF_SIZE 65536

static HWND g_hwnd = NULL;
static clipboard_on_text_t g_cb = NULL;
static void *g_userdata = NULL;
static char *g_last = NULL;
static int g_ready = 0;

static const char *read_clip(void)
{
    static char buf[CLIP_BUF_SIZE];
    HANDLE h;
    const wchar_t *w;
    int n;

    if (!g_hwnd || !OpenClipboard(g_hwnd))
        return NULL;
    h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return NULL;
    }
    w = (const wchar_t *)GlobalLock(h);
    if (!w) {
        CloseClipboard();
        return NULL;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0 || n >= CLIP_BUF_SIZE) {
        GlobalUnlock(h);
        CloseClipboard();
        return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, CLIP_BUF_SIZE, NULL, NULL);
    GlobalUnlock(h);
    CloseClipboard();
    return buf;
}

int clipboard_poll(void)
{
    const char *t;

    if (!g_cb)
        return 0;
    t = read_clip();
    if (!t || !*t)
        return 0;
    if (!g_ready) {
        /* 首次读取只做基线，不触发回调，避免开启瞬间重发已有内容 */
        free(g_last);
        g_last = _strdup(t);
        g_ready = 1;
        return 0;
    }
    if (g_last && strcmp(g_last, t) == 0)
        return 0;
    free(g_last);
    g_last = _strdup(t);
    g_cb(t, g_userdata);
    bt_log("clipboard: 剪切板变化 %zu 字节", strlen(t));
    return 1;
}

int clipboard_start(void *hwnd, clipboard_on_text_t cb, void *userdata)
{
    clipboard_stop();
    g_hwnd = (HWND)hwnd;
    g_cb = cb;
    g_userdata = userdata;
    g_ready = 0;
    if (g_hwnd) {
        AddClipboardFormatListener(g_hwnd);
        SetTimer(g_hwnd, POLL_TIMER_ID, POLL_MS, NULL);
    }
    bt_log("clipboard: Windows 监听启动");
    return 0;
}

void clipboard_stop(void)
{
    if (g_hwnd) {
        RemoveClipboardFormatListener(g_hwnd);
        KillTimer(g_hwnd, POLL_TIMER_ID);
    }
    g_hwnd = NULL;
    g_cb = NULL;
    g_userdata = NULL;
    g_ready = 0;
    free(g_last);
    g_last = NULL;
    bt_log("clipboard: Windows 监听停止");
}
