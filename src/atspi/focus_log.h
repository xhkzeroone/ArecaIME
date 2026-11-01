#pragma once

#include <cstdarg>
#include <cstdio>
#include <time.h>
#include <unistd.h>

namespace areca {

inline void focusLog(const char *format, ...) {
    if (access("/tmp/areca_debug", F_OK) != 0) return;

    static FILE *file = fopen("/tmp/areca_focus.log", "a");
    if (!file) return;

    struct timespec timestamp;
    clock_gettime(CLOCK_REALTIME, &timestamp);
    struct tm localTime;
    localtime_r(&timestamp.tv_sec, &localTime);
    fprintf(file, "[focus %02d:%02d:%02d.%03ld] ", localTime.tm_hour,
            localTime.tm_min, localTime.tm_sec,
            timestamp.tv_nsec / 1000000);

    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
}

} // namespace areca

#define FOCUS_LOG(...) ::areca::focusLog(__VA_ARGS__)