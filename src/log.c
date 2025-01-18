#include <compiler.h>
#include <log.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#define NANOPRINTF_VISIBILITY_STATIC
#define NANOPRINTF_IMPLEMENTATION
#include <nanoprintf.h>

/* <Configurable_values without code changes> */
#define WARN_ON_OVERFLOW
#define OVERFLOW_MSG "[DEBUG]: Log message truncated (overflow)\n"	/* Should have tag */
#define LINE_BUF_SIZE 2048	/* Don't make too large, it's allocated on stack, possibly a few times. */
#define DYNAMIC_LINE_SIZE	/* Dynamically determine line size. Slower. LINE_BUF_SIZE then used as a limit to not overflow stack */
#define DEFAULT_LOG_LEVEL LOG_INFO
static const char *log_tags[] = { "", "[ERROR]: ", "[WARNING]: ", "[INFO]: ", "[DEBUG]: ", "[REMOTE]: " };	/* Only content of strings may be changed */
/* </Configurable_values> */

#define timestamp_size 26	/* Based on definition of asctime. Includes NULL byte */
static bool redirected_stdio = false; 	/* If it wasn't redirected (yet), bypass log-like formatting */
static atomic_int _timezone = 0;			/* Offset in hours from UTC */
static int log_level = DEFAULT_LOG_LEVEL;
#ifdef WARN_ON_OVERFLOW
static_assert(LINE_BUF_SIZE >= timestamp_size+MAX(sizeof(log_tags[DEFAULT_LOG_LEVEL]), sizeof(OVERFLOW_MSG))-1, "LINE_BUF_SIZE is below the minimal size required for safe operation (asctime_r, strcat, recursive call on buffer overflow)");
#else
static_assert(LINE_BUF_SIZE >= timestamp_size+sizeof(log_tags[DEFAULT_LOG_LEVEL])-1, "LINE_BUF_SIZE is below the minimal size required for safe operation (asctime_r, strcat, recursive call on buffer overflow)");
#endif
static_assert(__STDC_VERSION__ > 201710L, "This program requires C23 because of the way variadic arguments are used.");

#define check(...)								\
	while (unlikely((__VA_ARGS__) < 0)) {		\
		if (errno != EINTR) {					\
			dlperror(#__VA_ARGS__);				\
		}										\
	}

typedef enum {
	ABORT,
	FALLBACK,
	DEFAULT,
	SPECIAL
} Action;

/* Simple localtime implementation without locks, taken from https://sourceware.org/bugzilla/show_bug.cgi?id=16145 and slightly adapted */
/* MT-safe AS-safe AC-safe */
static void localtime_safe(time_t time, struct tm *tm_time)
{
	const char Days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t n32_Pass4year;
	uint32_t n32_hpery;

	time = time + atomic_load_explicit(&_timezone, memory_order_relaxed);

	if (time < 0)
		time = 0;
	tm_time->tm_sec = (int)(time % 60);
	time /= 60;
	tm_time->tm_min = (int)(time % 60);
	time /= 60;
	n32_Pass4year = ((unsigned int)time / (1461L * 24L));
	tm_time->tm_year = (n32_Pass4year << 2) + 70;
	time %= 1461L * 24L;
	for (;;) {
		n32_hpery = 365 * 24;
		if ((tm_time->tm_year & 3) == 0)
			n32_hpery += 24;
		if (time < n32_hpery)
			break;
		tm_time->tm_year++;
		time -= n32_hpery;
	}
	tm_time->tm_hour = (int)(time % 24);
	time /= 24;
	time++;
	if ((tm_time->tm_year & 3) == 0) {
		if (time > 60) {
			time--;
		}
		else {
			if (time == 60) {
				tm_time->tm_mon = 1;
				tm_time->tm_mday = 29;
				return;
			}
		}
	}
	for (tm_time->tm_mon = 0; Days[tm_time->tm_mon] < time; tm_time->tm_mon++) {
		time -= Days[tm_time->tm_mon];
	}

	tm_time->tm_mday = (int)(time);
	return;
}

/* MT-safe locale | AS-safe | AC-safe */
static int init_line_buffer(char *line_buffer, bool addDefaultTag)
{
	time_t rawtime;
	struct tm local_time;
	
	if (unlikely(time(&rawtime) == -1))
		return 0;
	localtime_safe(rawtime, &local_time);
	/* https://www.gnu.org/software/libc/manual/2.38/html_node/Formatting-Calendar-Time.html */
	if (unlikely(asctime_r(&local_time, line_buffer)))
		strcpy(line_buffer, "Thu Jan 01 00:00:00 1970\n");
	if (addDefaultTag) {
		strcat(line_buffer, log_tags[DEFAULT_LOG_LEVEL]);
		return timestamp_size + sizeof(log_tags[DEFAULT_LOG_LEVEL]) - 2;
	}
	return timestamp_size - 1;
}

/* MT-safe | AS-safe | AC-safe */
static Action check_lprintf_format(const char *format)
{
	Action a;

	/* 
	 * DEFAULT - to stdout_fileno
	 * SPECIAL - to stderr_fileno
	*/

	if (format[0] != '[')
		a = FALLBACK;
	else switch(format[1]) {
		case 'D':
			a = SPECIAL;
			if (log_level < LOG_DEBUG)
				a = ABORT;
			break;
		case 'I':
			a = DEFAULT;
			if (log_level < LOG_INFO)
				a = ABORT;
			break;
		case 'W':
			a = SPECIAL;
			if (log_level < LOG_WARNING)
				a = ABORT;
			break;
		case 'E':
			a = SPECIAL;
			if (log_level < LOG_ERROR)
				a = ABORT;
			break;
		case 'R':	/* Message from remote peer */
			a = DEFAULT;
			break;
		default:
			a = FALLBACK;
	}
	
	return a;
}

/* POSIX.1-2008/SUSv4 Section XSI 2.9.7 ("Thread Interactions with Regular File Operations") -> write(2) is atomic on regular files */
/* MT-safe | AS-safe | AC-safe */
inline int simple_write(int fd, const char *buf)
{
	return write(fd, buf, strlen(buf));
}

/* MT-safe locale | AS-safe | AC-safe */
int lprintf(const char *format, ...)
{
	va_list ptr;
	int ret;

	va_start(ptr);
	ret = lvfprintf(NULL, format, ptr);
	va_end(ptr);

	return ret;
}

/* MT-safe locale | AS-safe | AC-safe */
int lfprintf(FILE *stream, const char *format, ...)
{
	va_list ptr;
	int ret;

	va_start(ptr);
	ret = lvfprintf(stream, format, ptr);
	va_end(ptr);

	return ret;
}

/* stream == NULL for default stream based on tag */
/* MT-safe locale | AS-safe | AC-safe */
int lvfprintf(FILE *stream, const char *format, va_list ap)
{
	#ifndef DYNAMIC_LINE_SIZE
		char line_buffer[LINE_BUF_SIZE];
	#endif
	int ret, olderrno = errno;
	Action a = check_lprintf_format(format);
	
	if (a == ABORT)
		return 0;
	#ifdef DYNAMIC_LINE_SIZE
		va_list ptr;
		va_copy(ptr, ap);
		char line_buffer[min(LINE_BUF_SIZE, npf_vsnprintf(NULL, 0, format, ptr)+timestamp_size+sizeof(log_tags[DEFAULT_LOG_LEVEL])-1)];
		va_end(ptr);
	#endif
	if (a == FALLBACK) {
		if (log_level < DEFAULT_LOG_LEVEL)
			return 0;
		ret = init_line_buffer(line_buffer, true);
	}
	else {
		ret = init_line_buffer(line_buffer, false);
	}

	ret = npf_vsnprintf(line_buffer+ret, sizeof(line_buffer)-ret, format, ap);
	#ifdef WARN_ON_OVERFLOW
		if (ret > sizeof(line_buffer)-ret-1)
			lprintf(OVERFLOW_MSG);
	#endif

	if (stream) {
		#ifdef DYNAMIC_LINE_SIZE
			ret = write(fileno(stream), line_buffer, sizeof(line_buffer));
		#else
			ret = simple_write(fileno(stream), line_buffer)
		#endif
	}
	else {
		#ifdef DYNAMIC_LINE_SIZE
			if (a == SPECIAL)
				ret = write(STDERR_FILENO, line_buffer, sizeof(line_buffer));
			else
				ret = write(STDOUT_FILENO, line_buffer, sizeof(line_buffer));
		#else
			if (a == SPECIAL)
				ret = simple_write(STDERR_FILENO, line_buffer);
			else
				ret = simple_write(STDOUT_FILENO, line_buffer);
		#endif
	}

	errno = olderrno;
	return ret;
}

/* MT-safe locale | AS-safe | AC-safe */
void lperrorf(const char *format, ...)
{
	int ret, olderrno = errno;
	va_list ptr;

	if (log_level < LOG_ERROR)
		return;

	#ifdef DYNAMIC_LINE_SIZE
		va_start(ptr);
		char error_message[min(LINE_BUF_SIZE, npf_vsnprintf(NULL, 0, format, ptr))];
		va_end(ptr);
	#else
		char error_message[LINE_BUF_SIZE];
	#endif

	va_start(ptr);
	ret = npf_vsnprintf(error_message, sizeof(error_message), format, ptr);
	va_end(ptr);
	#ifdef WARN_ON_OVERFLOW
		if (ret > sizeof(error_message)-1)
			lprintf(OVERFLOW_MSG);
	#endif
	lprintf("[ERROR]: %s: %s\n", error_message, strerrordesc_np(olderrno));

	errno = olderrno;
}

/* MT-Safe env locale | AS-Unsafe heap lock | AC-Unsafe lock mem fd */
void update_timezone()
{
	time_t rawtime = 0;
	struct tm local_time;

	if (!localtime_r(&rawtime, &local_time))
		dlperror("localtime");
	else
		atomic_store_explicit(&_timezone, local_time.tm_gmtoff, memory_order_relaxed);
}

/* MT-Safe env locale | AS-Unsafe heap lock | AC-Unsafe lock mem fd */
void setup_lstdio()
{
	char *log_level_str;

	update_timezone();
	log_level_str = getenv("LOG_LEVEL");
	if (log_level_str) {
		switch (log_level_str[0]) {
			case 'N':
			case 'n':
				log_level = LOG_NONE;
				break;
			case 'E':
			case 'e':
				log_level = LOG_ERROR;
				break;
			case 'W':
			case 'w':
				log_level = LOG_WARNING;
				break;
			case 'I':
			case 'i':
				log_level = LOG_INFO;
				break;
			case 'D':
			case 'd':
				log_level = LOG_DEBUG;
				break;
			default:
				log_level = DEFAULT_LOG_LEVEL;
				lprintf("[WARNING]: Unknown LOG_LEVEL value. Using the default value: LOG_WARNING.\n");
		}
	}
}

void redirect_stdio(char *log_path)
{
	int fd, nullfd;

	fd = open(log_path, O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC, 0600);
	check( nullfd = open("/dev/null", O_RDWR | O_CLOEXEC) )
	if (fd < 0) {
		dlperror("open");
		lprintf("[WARNING]: Cannot open %s\n", log_path);
		lprintf("[WARNING]: No logging functionality present.\n");
		fd = nullfd;
	}
	check( dup2(nullfd, STDIN_FILENO) )
	check( dup2(fd, STDOUT_FILENO) )
	check( dup2(fd, STDERR_FILENO) )

	if (likely(nullfd > STDERR_FILENO))
		check( close(nullfd) )
	if (likely(fd > STDERR_FILENO) && fd != nullfd)
		check( close(fd) )
	redirected_stdio = true;
}