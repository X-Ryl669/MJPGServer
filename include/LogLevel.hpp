/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */

#pragma once

/** The log level for message output */
enum LogLevel
{
   Default     =  0, //!< Default log level is like Info
   Debug       = -1, //!< Debug log level gives all information
   Info        =  0, //!< Only output useful information for usage
   Warning     =  1, //!< Be silent, except for errors and warnings
   Error       =  2, //!< Be silent, except for errors
   Silent      =  3, //!< Be silent.
};

/** The global log level that's used everywhere */
extern int logLevel;


/** Log information to the appropriate output. Use printf like formatting here */
int log(LogLevel level, const char * format, ...) 
#ifdef __GNUC__
__attribute__ ((format (printf, 2, 3)))
#endif
;
