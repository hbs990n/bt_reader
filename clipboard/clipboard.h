#ifndef CLIPBOARD_H
#define CLIPBOARD_H

/*
 * clipboard 模块 — 监听本机剪切板
 *
 * 剪切板文本变化时，在主线程调用回调（去重：仅文本确实变化且非空才触发）。
 * 本模块不关心蓝牙，只负责把新文本交出去；由 ui 模块决定如何处理。
 *
 * Linux   : GTK3 主循环定时轮询（gtk_clipboard_request_text，异步、线程安全）
 * Windows : 剪贴板格式监听 + 定时轮询兜底
 */

/* 有新剪切板文本时在主线程调用，text 非空、以 \0 结尾 */
typedef void (*clipboard_on_text_t)(const char *text, void *userdata);

/* 启动监听（同一时刻只允许一个实例）。
 * hwnd: Windows 传 GUI 窗口句柄（监听消息/定时器宿主），其他平台传 NULL。
 * 成功 0，失败 -1 */
int clipboard_start(void *hwnd, clipboard_on_text_t cb, void *userdata);

/* 停止监听并释放内部状态 */
void clipboard_stop(void);

/* Windows 专用：由 GUI 在 WM_TIMER / WM_CLIPBOARDUPDATE 中调用，主动检查一次
 * 剪切板。其他平台可直接调用（由内部轮询驱动）。有变化并触发回调返回 1，否则 0 */
int clipboard_poll(void);

#endif /* CLIPBOARD_H */
