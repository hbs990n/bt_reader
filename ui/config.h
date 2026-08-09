#ifndef CONFIG_H
#define CONFIG_H

/*
 * ui 模块 — 配置读写
 *
 * 保存用户偏好，下次启动自动恢复：
 *   mac              上次使用的手机 MAC
 *   clipboard_enabled 剪切板朗读开关（0/1）
 *
 * 配置文件:
 *   Linux   $XDG_CONFIG_HOME/bt_reader.conf 或 ~/.config/bt_reader.conf
 *   Windows %APPDATA%\bt_reader.conf
 */

typedef struct {
    char mac[64];
    int  clipboard_enabled;
} app_config;

/* 加载配置；文件不存在时填默认值（mac 空、clipboard 关） */
void config_load(app_config *cfg);

/* 保存配置；失败静默忽略 */
void config_save(const app_config *cfg);

#endif /* CONFIG_H */
