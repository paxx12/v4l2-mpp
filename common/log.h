#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

static void log_get_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "[%H:%M:%S]", tm_info);
}

static void log_fprintf_internal(FILE *stream, const char *format, ...)
{
    char timestamp[16];
    log_get_timestamp(timestamp, sizeof(timestamp));
    fprintf(stream, "%s ", timestamp);
    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
    fflush(stream);
}

#define log_printf(...) log_fprintf_internal(stdout, __VA_ARGS__)
#define log_errorf(...) log_fprintf_internal(stderr, __VA_ARGS__)
#define log_perror(s)   log_fprintf_internal(stderr, "%s: %s\n", s, strerror(errno))

#endif

