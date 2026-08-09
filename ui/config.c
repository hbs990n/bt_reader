#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static void config_path(char *buf, size_t sz)
{
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata)
        snprintf(buf, sz, "%s\\bt_reader.conf", appdata);
    else
        snprintf(buf, sz, "bt_reader.conf");
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(buf, sz, "%s/bt_reader.conf", xdg);
    } else if (home && *home) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/.config", home);
        mkdir(dir, 0755); /* 尽力而为，已存在则忽略 */
        snprintf(buf, sz, "%s/.config/bt_reader.conf", home);
    } else {
        snprintf(buf, sz, "bt_reader.conf");
    }
#endif
}

void config_load(app_config *cfg)
{
    char path[1024];
    FILE *f;

    memset(cfg, 0, sizeof(*cfg));
    config_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return;
    {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char key[64], val[384];
            if (sscanf(line, "%63[^=]=%383[^\n]", key, val) == 2) {
                if (strcmp(key, "mac") == 0) {
                    strncpy(cfg->mac, val, sizeof(cfg->mac) - 1);
                    cfg->mac[sizeof(cfg->mac) - 1] = '\0';
                } else if (strcmp(key, "clipboard") == 0) {
                    cfg->clipboard_enabled = (strcmp(val, "1") == 0) ? 1 : 0;
                }
            }
        }
    }
    fclose(f);
}

void config_save(const app_config *cfg)
{
    char path[1024];
    FILE *f;

    config_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "mac=%s\n", cfg->mac ? cfg->mac : "");
    fprintf(f, "clipboard=%d\n", cfg->clipboard_enabled ? 1 : 0);
    fclose(f);
}
