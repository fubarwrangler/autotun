#ifndef _UTIL_H__
#define _UTIL_H__

#include <stdio.h>

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


struct fd_map {
	size_t len;
	void **ptrs;
};

struct fd_map *new_fdmap(void);
int add_fdmap(struct fd_map *m, int i, void *p);
void *get_fdmap(struct fd_map *m, int idx);
void remove_fdmap(struct fd_map *m, int idx);
void del_fdmap(struct fd_map *fd);


extern int _debug;
extern char *prog_name;
extern int _verbose;
extern FILE *debug_stream;


#endif /* _UTIL_H__ */
