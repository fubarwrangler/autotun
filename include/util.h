#ifndef _UTIL_H__
#define _UTIL_H__

enum error_exit_codes {
	NO_ERROR,
	MEMORY_ERROR,
	CONFIG_ERROR,
};

void log_msg(const char *fmt, ...);
void log_exit(int code, const char *fmt, ...) __attribute__((noreturn));
void log_exit_perror(int code, const char *fmt, ...) __attribute__((noreturn));
void *safemalloc(size_t size, const char *fail);
void saferealloc(void **p, size_t new_size, const char *fail);
char *safestrdup(const char *str, const char *fail);
void debug(const char *fmt, ...);

extern int _debug;
extern char *prog_name;
extern int _verbose;


#endif /* _UTIL_H__ */
