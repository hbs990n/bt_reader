#include "bt_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <signal.h>
#endif

static const char *log_path(void)
{
    const char *p = getenv("BT_READER_LOG");
    if (p && *p)
        return p;
#ifndef _WIN32
    return "/tmp/bt_reader.log";
#else
    return "bt_reader.log";
#endif
}

void bt_log(const char *fmt, ...)
{
    FILE *f = fopen(log_path(), "a");
    char ts[32];
    time_t t;
    struct tm *tm;
    va_list ap;

    if (!f)
        return;
    t = time(NULL);
    tm = localtime(&t);
    strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", tm);
    fprintf(f, "[%s] ", ts);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

#ifndef _WIN32
static void crash_handler(int sig)
{
    void *frames[32];
    int n = backtrace(frames, 32);
    int fd = open(log_path(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "\n==== CRASH signal %d ====\n", sig);
        (void)write(fd, buf, (size_t)len);
        backtrace_symbols_fd(frames, n, fd);
        (void)write(fd, "\n", 1);
        close(fd);
    }
    _exit(128 + sig);
}

void bt_install_crash_handler(void)
{
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
}
#else
void bt_install_crash_handler(void) {}
#endif
