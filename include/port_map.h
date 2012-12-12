#ifndef _PORT_MAP__
#define _PORT_MAP__

#include <libssh/libssh.h>
#include "autotun.h"


struct chan_sock {
	ssh_channel channel;
	int sock_fd;
	struct static_port_map *parent;
};

struct dynamic_chan_sock {
	ssh_channel channel;
	int sock_fd;
	struct static_port_map *parent;
	int socks_state;
};

struct static_port_map {
	int listen_fd;
	uint32_t local_port;
	char *remote_host;
	uint32_t remote_port;
	union {
		struct chan_sock **cs;
		struct dynamic_chan_sock **dyn;
	} ch;
	int n_channels;
	struct gw_host *parent;
};

void add_map_to_gw(struct gw_host *gw, uint32_t local_port,
				   char *host, uint32_t remote_port);
struct chan_sock *
add_channel_to_map(struct static_port_map *pm,
				   ssh_channel channel,
				   int sock_fd);
int connect_forward_channel(struct chan_sock *cs);
void remove_channel_from_map(struct chan_sock *cs);
void remove_map_from_gw(struct static_port_map *map);

#endif
