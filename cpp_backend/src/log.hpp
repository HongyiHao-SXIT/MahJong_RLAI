#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <map>
#include <memory>
#include <functional>
#include <cstdarg>

// ----------------------------------------------------------------
// Logging
// ----------------------------------------------------------------

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

inline LogLevel g_log_level = LOG_INFO;

inline void log_set_level(LogLevel level) { g_log_level = level; }

inline void log_msg(LogLevel level, const char* fmt, ...) {
    if (level < g_log_level) return;
    const char* prefix = "";
    va_list args;
    va_start(args, fmt);
    switch (level) {
        case LOG_DEBUG: prefix = "[DEBUG] "; break;
        case LOG_INFO:  prefix = "[INFO]  "; break;
        case LOG_WARN:  prefix = "[WARN]  "; break;
        case LOG_ERROR: prefix = "[ERROR] "; break;
    }
    fprintf(stderr, "%s", prefix);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

#define LOG_DEBUG(fmt, ...) log_msg(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_msg(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_msg(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(LOG_ERROR, fmt, ##__VA_ARGS__)