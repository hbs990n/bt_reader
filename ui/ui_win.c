/*
 * ui_win.c — Windows Win32 图形界面
 *
 * 功能:
 *   1. 输入 MAC / 输入文本 → 连接手机并朗读
 *   2. 「剪切板朗读」开关：监听本机剪贴板，文本变化自动发送朗读
 *   3. MAC 与剪切板开关状态持久化到配置文件
 *
 * 配对请走系统设置（本程序提供说明按钮）；连接发送用 RFCOMM（bt 模块）。
 */

#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <string.h>
#include <stdlib.h>

#include "bt.h"
#include "config.h"
#include "clipboard.h"
#include "bt_log.h"
#include "ui.h"

#define IDC_MAC 101
#define IDC_TEXT 102
#define IDC_PAIR 103
#define IDC_SEND 104
#define IDC_CLIP 105
#define WM_APP_DONE (WM_APP + 1)
#define WM_APP_CLIP_DONE (WM_APP + 2)

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

static HWND hwnd;
static HWND hMac;
static HWND hText;
static HWND hBtnSend;
static HWND hStatus;
static HWND hBtnClip;

static app_config g_cfg;
static char g_clip_mac[64];  /* 开启剪切板朗读时锁定的 MAC */
static int  g_clip_gen;      /* 剪切板文本代数：新文本+1，旧发送任务过期作废 */

/* ---- 通用小工具 ---- */

static void set_child_font(HWND child)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
}

static void get_mac_utf8(char *out, size_t sz)
{
    wchar_t macw[64];
    out[0] = '\0';
    GetWindowTextW(hMac, macw, 64);
    WideCharToMultiByte(CP_UTF8, 0, macw, -1, out, (int)sz, NULL, NULL);
    out[sz - 1] = '\0';
}

static void set_mac_from_utf8(const char *mac)
{
    wchar_t macw[64];
    MultiByteToWideChar(CP_UTF8, 0, mac, -1, macw, 64);
    SetWindowTextW(hMac, macw);
}

/* ---- 手动发送 ---- */

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
    PostMessageW(hwnd, WM_APP_DONE, (WPARAM)d, 0);
    return 0;
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

    strncpy(g_cfg.mac, d->mac, sizeof(g_cfg.mac) - 1);
    g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';

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

/* ---- 剪切板朗读 ---- */

typedef struct {
    char mac[64];
    char *text;
    int gen;
    int rc;
} wclip;

static DWORD WINAPI wclip_worker(LPVOID p)
{
    wclip *d = p;
    int fd = -1;

    if (d->gen != g_clip_gen) {
        bt_log("clipboard: 发送前发现已被新内容废除");
        free(d->text);
        free(d);
        return 0;
    }
    if (bt_init() == 0) {
        fd = bt_open(d->mac, BT_DEFAULT_CHANNEL);
        if (fd >= 0) {
            if (d->gen == g_clip_gen) {
                bt_log("clipboard: 连接 %s，文本 %zu 字节", d->mac, strlen(d->text));
                d->rc = bt_send_line(fd, d->text, strlen(d->text));
            }
        } else {
            d->rc = -2; /* 连接失败 */
        }
        if (fd >= 0)
            bt_close(fd);
        bt_cleanup();
    } else {
        d->rc = -3; /* 初始化失败 */
    }
    if (d->gen != g_clip_gen) {
        /* 发送期间又来了新文本：本次作废，不通知界面 */
        bt_log("clipboard: 发送期间被新内容废除");
        free(d->text);
        free(d);
        return 0;
    }
    PostMessageW(hwnd, WM_APP_CLIP_DONE, (WPARAM)d, 0);
    return 0;
}

static void on_clip_text(const char *text, void *userdata)
{
    (void)userdata;
    if (!*g_clip_mac) {
        bt_log("clipboard: 无有效 MAC，忽略");
        return;
    }
    /* 新内容：作废所有旧文本的发送，永远以最新为准 */
    g_clip_gen++;
    wclip *d = calloc(1, sizeof(*d));
    if (!d)
        return;
    strncpy(d->mac, g_clip_mac, sizeof(d->mac) - 1);
    d->mac[sizeof(d->mac) - 1] = '\0';
    d->text = _strdup(text);
    d->gen = g_clip_gen;
    SetWindowTextW(hStatus, L"剪切板变化，正在连接手机...");
    {
        HANDLE th = CreateThread(NULL, 0, wclip_worker, d, 0, NULL);
        if (th) {
            CloseHandle(th);
        } else {
            free(d->text);
            free(d);
        }
    }
}

/* 根据开关状态启动/停止剪切板监听 */
static void apply_clip_config(void)
{
    BOOL on = (SendMessageW(hBtnClip, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (on) {
        char mac[64];
        get_mac_utf8(mac, sizeof(mac));
        if (!*mac) {
            SendMessageW(hBtnClip, BM_SETCHECK, BST_UNCHECKED, 0);
            MessageBoxW(hwnd, L"请先填写手机 MAC 再开启剪切板朗读", L"提示",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
        strncpy(g_cfg.mac, mac, sizeof(g_cfg.mac) - 1);
        g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';
        strncpy(g_clip_mac, mac, sizeof(g_clip_mac) - 1);
        g_clip_mac[sizeof(g_clip_mac) - 1] = '\0';
        if (clipboard_start(hwnd, on_clip_text, NULL) == 0) {
            g_cfg.clipboard_enabled = 1;
            config_save(&g_cfg);
            SetWindowTextW(hStatus, L"剪切板朗读已开启（复制文本即自动朗读）");
        } else {
            SendMessageW(hBtnClip, BM_SETCHECK, BST_UNCHECKED, 0);
            SetWindowTextW(hStatus, L"剪切板监听启动失败");
        }
    } else {
        clipboard_stop();
        g_clip_gen = 0;
        g_cfg.clipboard_enabled = 0;
        config_save(&g_cfg);
        SetWindowTextW(hStatus, L"剪切板朗读已关闭");
    }
}

/* ---- 消息循环 ---- */

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
        hBtnClip = CreateWindowExW(0, L"BUTTON", L"剪切板朗读",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            238, 46, 120, 22, hw, (HMENU)IDC_CLIP, NULL, NULL);
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
        set_child_font(hBtnClip);
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
        case IDC_CLIP:
            if (HIWORD(wp) == BN_CLICKED)
                apply_clip_config();
            break;
        }
        return 0;
    case WM_CLIPBOARDUPDATE:
        clipboard_poll();
        return 0;
    case WM_TIMER:
        clipboard_poll();
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
    case WM_APP_CLIP_DONE: {
        wclip *d = (wclip *)wp;
        SetWindowTextW(hStatus, d->rc == 0 ? L"剪切板已发送，手机应开始朗读"
                                          : L"剪切板发送失败（请确认手机已开启「蓝牙朗读」且已配对）");
        free(d->text);
        free(d);
        return 0;
    }
    case WM_DESTROY:
        clipboard_stop();
        g_clip_gen = 0;
        {
            char mac[64];
            get_mac_utf8(mac, sizeof(mac));
            strncpy(g_cfg.mac, mac, sizeof(g_cfg.mac) - 1);
            g_cfg.mac[sizeof(g_cfg.mac) - 1] = '\0';
            config_save(&g_cfg);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int ui_run_gui(int argc, char **argv)
{
    WNDCLASSW wc;
    MSG msg;

    (void)argc;
    (void)argv;

    config_load(&g_cfg);

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

    if (*g_cfg.mac)
        set_mac_from_utf8(g_cfg.mac);
    if (g_cfg.clipboard_enabled) {
        SendMessageW(hBtnClip, BM_SETCHECK, BST_CHECKED, 0);
        apply_clip_config();
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
