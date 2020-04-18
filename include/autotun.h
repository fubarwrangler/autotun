#ifndef _AUTOTUN_H__
#define _AUTOTUN_H__

#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

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
	char *auth;
	int local;
	int n_maps;
	struct fd_map *chan_sock_fdmap;
	struct static_port_map **pm;
	struct fd_map *listen_fdmap;
};

void setup_signals_for_child(void);
void setup_signals_parent(void);
int select_loop(struct gw_host *gw);
struct gw_host *create_gw(const char *hostname);
int run_gateway(struct gw_host *gw);
void destroy_gw(struct gw_host *gw);

extern bool finish_main_loop;
extern bool hard_shutdown;

#endif
