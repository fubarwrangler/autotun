#ifndef _AUTOTUN_H__
#define _AUTOTUN_H__

#include <stdbool.h>

#include "util.h"
#include <libssh/libssh.h>

enum session_stat_vars {
	NOT_CREATED,
	NOT_AUTHENTICATED,
	OK,
	ERROR,
};

struct gw_host {
	char *name;
	ssh_session session;
	bool compression;
	bool close_on_failure;
	struct fd_map *listen_fdmap;
	int n_maps;
	struct fd_map *chan_sock_fdmap;
	struct static_port_map **pm;
};


int select_loop(struct gw_host *gw);
struct gw_host *create_gw(const char *hostname);

extern int end_ssh_select_loop;

#endif
