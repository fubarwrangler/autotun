#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#include "util.h"

static inline void log_msg_init(void)
{
	char p[64];
	time_t t = time(NULL);

	if(strftime(p, 63, "%m/%d %X", localtime(&t)) > 0)
		fprintf(stderr, "%s %s: ", p, prog_name);
}


void log_exit_perror(int code, const char *fmt, ...)
{
	char buf[2048];
	va_list ap;

	log_msg_init();
	va_start(ap, fmt);
	vsnprintf(buf, 2046, fmt, ap);
	va_end(ap);
	perror(buf);
	exit(code);
}

void log_exit(int code, const char *fmt, ...)
{
	va_list ap;
	log_msg_init();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(code);
}

void log_msg(const char *fmt, ...)
{
	va_list ap;
	log_msg_init();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void debug(const char *fmt, ...)
{
	va_list ap;
	if(_debug != 0)	{
		log_msg_init();
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fputc('\n', stderr);
	}
}

/* Returns zeroed block */
void *safemalloc(size_t size, const char *fail)
{
	void *p = malloc(size);
	if(p == NULL)
		log_exit(MEMORY_ERROR, "safemalloc error: %s", fail);
	memset(p, 0, size);
	return p;
}


void saferealloc(void **p, size_t new_size, const char *fail)
{
	void *tmp = realloc(*p, new_size);
	if(tmp == NULL)
		log_exit(MEMORY_ERROR, "realloc failed: %s", fail);
	*p = tmp;
}

char *safestrdup(const char *str, const char *fail)
{
	char *p = strdup(str);
	if(p == NULL)
		log_exit(MEMORY_ERROR, "strdup failed: %s", fail);
	return p;
}

struct fd_map *new_fdmap(void)
{
	struct fd_map *fd;
	fd = safemalloc(sizeof(struct fd_map), "new fdmap");
	fd->ptrs = safemalloc(1 * sizeof(void *), "new fdmap ptrarray");
	fd->ptrs[0] = NULL;
	fd->len = 1;
	return fd;
}

int add_fdmap(struct fd_map *m, int i, void *p)
{
	if(m->len <= i)	{
		saferealloc((void **)&m->ptrs, (i + 1) * sizeof(void *), "fdmap: grow");
		for(int j = m->len; j < i; j++)
			m->ptrs[j] = NULL;
		m->len = i + 1;
	}
	m->ptrs[i] = p;
	return 0;
}

void *get_fdmap(struct fd_map *m, int idx)
{
	if(m->len <= idx)
		log_exit(1, "Error: fdmap asked for %d, only %d long", idx, m->len);
	return m->ptrs[idx];
}

void remove_fdmap(struct fd_map *m, int idx)
{
	m->ptrs[idx] = NULL;
	if(idx + 1 == m->len)	{
		int i;
		for(i = m->len - 1; i > 0; i--)	{
			if(m->ptrs[i] != NULL)
				break;
		}
		saferealloc((void **)&m->ptrs, (i + 1) * sizeof(void *), "fdmap: shrink");
		m->len = i + 1;
	}
}

void del_fdmap(struct fd_map *fd)
{
	if(fd->ptrs != NULL)
		free(fd->ptrs);
	free(fd);
}


