#ifndef BT_LOG_H
#define BT_LOG_H

/*
 * 简单文件日志：默认写入 /tmp/bt_reader.log，可用环境变量 BT_READER_LOG 改路径。
 * Linux 下可安装崩溃处理器，把信号 + 调用栈写入同一日志。
 */

void bt_log(const char *fmt, ...);
void bt_install_crash_handler(void);

#endif /* BT_LOG_H */
