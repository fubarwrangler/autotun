#ifndef _PORT_MAP__
#define _PORT_MAP__

#include <libssh/libssh.h>
#include "autotun.h"

struct static_port_map {
	char *gw; /* Redundant but must be same as gw_host->name */
	uint32_t local_port;
	char *remote_host;
	uint32_t remote_port;
	ssh_channel *channels;
	int n_channels;
	int socket_fd;
};

int add_map_to_gw(struct gw_host *gw, uint32_t local_port,
				   char *host, uint32_t remote_port);
void add_channel_to_map(struct static_port_map *pm, ssh_channel channel);
int connect_forward_channel(struct static_port_map *pm, int idx);
int remove_channel_from_map(struct static_port_map *pm, ssh_channel ch);
int remove_map_from_gw(struct gw_host *gw, struct static_port_map *map);
void free_map(struct static_port_map *pm);

#endif
