// We need our declaration
#include "../include/LogLevel.hpp"
#include <stdio.h>
#include <stdarg.h>


#ifdef SkipClassPathLogger 
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
#else
#include "Logger/Logger.hpp"
int log(LogLevel level, const char * format, ...) 
{
    if (level < logLevel) return 0; // Skip logging if set as-is
    va_list list;
    va_start(list, format);
    char * buffer = 0;
    // We use vasprintf extension to avoid dual parsing of the format string to find out the required length
    const int err = vasprintf(&buffer, format, list);
    if (err <= 0) return err;
    va_end(list);

    // Convert from LogLevel to ClassPath's flags here
    int flags[] = {
        Logger::AllFlags, // Debug
        Logger::Content | Logger::Network, // Default/Info
        Logger::Warning | Logger::Content | Logger::Network, // Warning
        Logger::Error | Logger::Content | Logger::Network, // Error 
        0, // No logs 
    };

    Logger::getDefaultSink().gotMessage(buffer, flags[(int)level + 1]);
    ::free(buffer);
    return err;    
}
#endif