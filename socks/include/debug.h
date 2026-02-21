#pragma once

#if _DEBUG == 1

#include <stdio.h>


#define DBG "DBG"
#define ERR "ERR"
#define WRN "WRN"
#define INF "INF"

#define _log_no_format(format, ...) printf(format, ## __VA_ARGS__);
#define _log(level, format, ...) printf("[%s] %s:%d - " format "\n", level, __FUNCTION__, __LINE__, ## __VA_ARGS__);
#define _dbg(format, ...) _log(DBG, format, ## __VA_ARGS__)
#define _err(format, ...) _log(ERR, format, ## __VA_ARGS__)
#define _wrn(format, ...) _log(WRN, format, ## __VA_ARGS__)
#define _inf(format, ...) _log(INF, format, ## __VA_ARGS__)

#define _log_no_newline(level, format, ...) printf("[%s] %s:%d - " format, level, __FUNCTION__, __LINE__, ## __VA_ARGS__);
#define _inf_no_newline(format, ...) _log_no_newline(INF, format, ## __VA_ARGS__)

#define API(x) x

#else

#define _log_no_format(format, ...)
#define _log(level, format, ...)
#define _dbg(format, ...)
#define _err(format, ...)
#define _wrn(format, ...)
#define _inf(format, ...)
#define _log_no_newline(level, format, ...)
#define _inf_no_newline(format, ...)

#ifndef API // If API is not defined, define it as a passthrough

#define API(x) x

#endif

#endif