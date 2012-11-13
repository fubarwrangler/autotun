#ifndef _FD_SET_MY__
#define _FS_SET_MY__

#include "autotun.h"

typedef struct my_fd {
	int *fds;
	int n;
} *my_fdset;

my_fdset new_fdset(void);
int add_fd_to_set(my_fdset set, int fd);
int remove_fd_from_set(my_fdset set, int fd);
int fd_in_set(my_fdset set, int fd);
void destroy_fdset(my_fdset set);




#endif
