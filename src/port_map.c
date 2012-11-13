#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>


#include "util.h"
#include "port_map.h"
#include "autotun.h"
#include "net.h"


int add_map_to_gw(struct gw_host *gw,
				  uint32_t local_port,
				  char *host,
				  uint32_t remote_port)
{
	struct static_port_map *spm;

	debug("Adding map %d %s:%d to %s", local_port, host, remote_port, gw->name);

	spm = safemalloc(sizeof(struct static_port_map), "static_port_map alloc");
	spm->gw = gw->name;
	spm->local_port = local_port;
	spm->remote_host = safestrdup(host, "spm strdup hostname");
	spm->remote_port = remote_port;
	spm->channels = safemalloc(sizeof(ssh_channel *), "spm->channels[0]");

	spm->listen_fd = create_listen_socket(local_port, "localhost");

	saferealloc((void **)&gw->pm, (gw->n_maps + 1) * sizeof(spm), "gw->pm realloc");
	gw->pm[gw->n_maps++] = spm;

	return 0;
}

void add_channel_to_map(struct static_port_map *pm,
						ssh_channel channel,
						int sock_fd)
{
	debug("Adding channel %p to map %s:%d", channel, pm->remote_host, pm->remote_port);
	saferealloc((void **)&pm->channels, (pm->n_channels + 1) * sizeof(channel),
				"pm->channel realloc");
	saferealloc((void **)&pm->chan_socks, (pm->n_channels + 1) * sizeof(channel),
				"pm->channel realloc");
	pm->channels[pm->n_channels] = channel;
	pm->chan_socks[pm->n_channels] = sock_fd;
	pm->n_channels++;
}

int remove_channel_from_map(struct static_port_map *pm, ssh_channel ch)
{
	int i;

	for(i = 0; i < pm->n_channels; i++)	{
		if(pm->channels[i] == ch)	{
			if(ssh_channel_is_open(ch) && ssh_channel_close(ch) != SSH_OK)
					log_msg("Error on channel close for %s", pm->gw);
			ssh_channel_free(ch);
			break;
		}
	}
	if(i == pm->n_channels) /* Channel not found in *pm */
		return 1;

	for(; i < pm->n_channels - 1; i++)
		pm->channels[i] = pm->channels[i + 1];

	saferealloc((void **)&pm->channels, (--pm->n_channels + 1) * sizeof(ch),
				"pm->channel realloc");
	return 0;
}

void free_map(struct static_port_map *pm)
{
	for(int i = 0; i < pm->n_channels; i++)	{
		if( ssh_channel_is_open(pm->channels[i]) &&
			ssh_channel_close(pm->channels[i]) != SSH_OK
		  )
			log_msg("Error on channel close for %s", pm->gw);
		ssh_channel_free(pm->channels[i]);
	}
	free(pm->channels);
	free(pm->remote_host);
	free(pm);
}


int connect_forward_channel(struct static_port_map *pm, int idx)
{
	int rc;
	rc = ssh_channel_open_forward(pm->channels[idx], pm->remote_host,
								  pm->remote_port, "localhost", pm->local_port);
	if(rc != SSH_OK)	{
		log_msg("Error: error opening forward %d -> %s:%d", pm->local_port,
				pm->remote_host, pm->remote_port);
		remove_channel_from_map(pm, pm->channels[idx]);
		return 1;
	}
	return 0;
}

/*void print_chan(struct static_port_map *m)
{
	for(int i = 0; i < m->n_channels; i++)
		printf("Chan %d (%p) open: %d\n", i, m->channels[i], ssh_channel_is_open(m->channels[i]));
}*/

/* Return index if *map is in gw->pm[] array, else -1 */
static int map_in_gw(struct gw_host *gw, struct static_port_map *map)
{
	for(int i = 0; i < gw->n_maps; i++)
		if(map == gw->pm[i])
			return i;
	return -1;
}

int remove_map_from_gw(struct gw_host *gw, struct static_port_map *map)
{
	int idx;

	if((idx = map_in_gw(gw, map)) < 0)	{
		log_msg("Error: map %p not found in gw->pm (%p)", map, gw->pm);
		return 1;
	}

	if(gw->n_maps >= 1)	{
		int i = idx;
		while(i < gw->n_maps - 1)	{
			gw->pm[i] = gw->pm[i + 1];
			i++;
		}
		gw->n_maps--;
		free_map(map);
	}
	return 0;
}



