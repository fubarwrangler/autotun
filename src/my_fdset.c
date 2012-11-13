#include <stdio.h>
#include <stdlib.h>

/* This is O(n) all over the place, for now (and probably forever)
 * this should be fine
 */

#include "my_fdset.h"
#include "util.h"


my_fdset new_fdset(void)
{
	my_fdset fds;
	fds = safemalloc(sizeof(*fds), "newfdset");
	fds->n = 0;
	fds->fds = NULL;
	return fds;
}

int add_fd_to_set(my_fdset set, int fd)
{
	if(fd_in_set(set, fd))
		return -1;

	saferealloc((void **)&set->fds, (set->n + 1) * sizeof(set->fds), "add fdset");
	set->fds[set->n] = fd;
	set->n++;
}

int fd_in_set(my_fdset set, int fd)
{
	for(int i = 0; i < set->n; i++)
		if(fd == set->fds[i])
			return 1;
	return 0;
}

int remove_fd_from_set(my_fdset set, int fd)
{
	int i;

	for(i = 0; i < set->n; i++)	{
		if(set->fds[i] == fd)
			break;
	}
	if(i == set->n)
		return -1;
	for(; i < set->n; i++)
		set->fds[i] = set->fds[i + 1];
	saferealloc((void **)&set->fds, set->n * sizeof(set->fds), "rm fdset");
	set->n--;

	return 0;
}

void destroy_fdset(my_fdset set)
{
	free(set->fds);
	free(set);
}

#define TEST_FD
#ifdef TEST_FD

int _debug;
char *prog_name;

static void print_fds(my_fdset set)
{
	printf("Set %d elements:\n", set->n);
	for(int i = 0; i < set->n; i++)
		printf("%d: %d\n", i, set->fds[i]);
	printf("***\n");
}

int main(int argc, char *argv[])
{
	my_fdset s = new_fdset();

	add_fd_to_set(s, 5);
	add_fd_to_set(s, 4);
	add_fd_to_set(s, 5);
	add_fd_to_set(s, 3);
	add_fd_to_set(s, 8);
	add_fd_to_set(s, 2);
	add_fd_to_set(s, 5);

	print_fds(s);

	remove_fd_from_set(s, 8);
	remove_fd_from_set(s, 4);

	print_fds(s);

	destroy_fdset(s);


	return 0;
}
#endif
