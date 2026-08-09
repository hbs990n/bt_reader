#ifndef UI_H
#define UI_H

/*
 * ui 模块 — 用户界面 + 配置
 *
 * Linux   : GTK3 图形界面 (ui_gtk.c)
 * Windows : Win32 图形界面 (ui_win.c)
 *
 * 由 main.c 调用，GUI 退出时返回 0。
 */

int ui_run_gui(int argc, char **argv);

#endif /* UI_H */
