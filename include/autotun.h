#ifndef _AUTOTUN_H__
#define _AUTOTUN_H__

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
	int n_maps;
	struct static_port_map **pm;
};


int select_loop(struct gw_host *gw);

extern int end_ssh_select_loop;

#endif
