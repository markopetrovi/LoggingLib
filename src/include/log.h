#ifndef _LOG_H
#define _LOG_H

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

#define LOG_NONE	0
#define LOG_ERROR	1
#define LOG_WARNING	2
#define LOG_INFO	3
#define LOG_DEBUG	4
#define LOG_REMOTE	5	/* Always print these messages; they're explicitly requested by the user */

#define LOG_ERROR_TAG   "[ERROR]: "
#define LOG_WARNING_TAG "[WARNING]: "
#define LOG_INFO_TAG    "[INFO]: "
#define LOG_DEBUG_TAG   "[DEBUG]: "
#define LOG_REMOTE_TAG  "[REMOTE]: "

#define LOG_TAG_SIZE(level)								\
    ((level) == LOG_ERROR ? sizeof(LOG_ERROR_TAG) :		\
    (level) == LOG_WARNING ? sizeof(LOG_WARNING_TAG) :	\
    (level) == LOG_INFO ? sizeof(LOG_INFO_TAG) :		\
    (level) == LOG_DEBUG ? sizeof(LOG_DEBUG_TAG) :		\
    (level) == LOG_REMOTE ? sizeof(LOG_REMOTE_TAG) : 0)

void redirect_stdio(char *log_path);

/* MT-Safe env locale | AS-Unsafe heap lock | AC-Unsafe lock mem fd */
void update_timezone();

/* MT-Safe env locale | AS-Unsafe heap lock | AC-Unsafe lock mem fd */
void setup_lstdio();

/* Applies to all below: MT-safe locale | AS-safe | AC-safe */
int lfprintf(FILE *stream, const char *format, ...);
int lprintf(const char *format, ...);
int lvfprintf(FILE *stream, const char *format, va_list ap);
void lperrorf(const char *format, ...);
#define lperror(message)	lprintf("[ERROR]: %s: %s\n", message, strerrordesc_np(errno))
#define dlperror(message)	lprintf("[ERROR]: %s:"TOSTRING(__LINE__)": %s: %s\n", basename(__FILE__), message, strerrordesc_np(errno))

#endif /* _LOG_H */
