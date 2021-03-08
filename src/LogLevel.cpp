// We need our declaration
#include "../include/LogLevel.hpp"
#include <stdio.h>
#include <stdarg.h>

int filteredLog(LogLevel level, const char * format, va_list list)
{
    if (level < logLevel) return 0; // Skip logging if set as-is

    FILE * o = level > 0 ? stderr : stdout;
    int ret = vfprintf(o, format, list); 
    fputs("\n", o);
    return ret + 1; 
}

// Log information to the appropriate output. Use printf like formatting here
int log(LogLevel level, const char * format, ...) 
{
    va_list list;
    va_start(list, format);
    int ret = filteredLog(level, format, list);
    va_end(list);
    return ret;
}