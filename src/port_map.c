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
	spm->parent = gw;
	spm->local_port = local_port;
	spm->remote_host = safestrdup(host, "spm strdup hostname");
	spm->remote_port = remote_port;
	spm->ch = safemalloc(sizeof(struct chan_sock *), "spm->ch");

	spm->listen_fd = create_listen_socket(local_port, "localhost");
	add_fdmap(gw->listen_fdmap, spm->listen_fd, spm);
	spm->parent = gw;

	saferealloc((void **)&gw->pm, (gw->n_maps + 1) * sizeof(spm), "gw->pm realloc");
	gw->pm[gw->n_maps++] = spm;

	return 0;
}

struct chan_sock *
add_channel_to_map(struct static_port_map *pm,
				   ssh_channel channel,
				   int sock_fd)
{
	struct chan_sock *cs = safemalloc(sizeof(struct chan_sock), "add ch cs");

	debug("Adding channel %p to map %s:%d", channel, pm->remote_host, pm->remote_port);
	saferealloc((void **)&pm->ch, (pm->n_channels + 1) * sizeof(pm->ch),
				"pm->channel realloc");
	cs->channel = channel;
	cs->sock_fd = sock_fd;
	add_fdmap(pm->parent->chan_sock_fdmap, sock_fd, cs);
	pm->ch[pm->n_channels] = cs;
	pm->n_channels++;
	cs->parent = pm;
	return cs;
}

int remove_channel_from_map(struct static_port_map *pm, struct chan_sock *cs)
{
	int i;

	if(cs->parent != pm)
		log_exit(1, "Corrupt chan_sock parent %p->parent should be %p", cs, pm);

	for(i = 0; i < pm->n_channels; i++)	{
		if(pm->ch[i] == cs)	{
			if( ssh_channel_is_open(cs->channel) &&
				ssh_channel_close(cs->channel) != SSH_OK)
					log_msg("Error on channel close for %s", pm->parent->name);
			ssh_channel_free(cs->channel);
			break;
		}
	}
	if(i == pm->n_channels) /* Channel not found in *pm */
		return 1;

	debug("Destroy channel %p, closing fd=%d", cs->channel, cs->sock_fd);
	for(; i < pm->n_channels - 1; i++)
		pm->ch[i] = pm->ch[i + 1];

	saferealloc((void **)&pm->ch, pm->n_channels * sizeof(cs),
				"pm->channel realloc");
	pm->n_channels -= 1;
	close(cs->sock_fd);
	remove_fdmap(cs->parent->parent->chan_sock_fdmap, cs->sock_fd);
	free(cs);
	return 0;
}

void free_map(struct static_port_map *pm)
{
	debug("Freeing map %p (listen on %d)", pm, pm->local_port);
	for(int i = 0; i < pm->n_channels; i++)	{
		if( ssh_channel_is_open(pm->ch[i]->channel) &&
			ssh_channel_close(pm->ch[i]->channel) != SSH_OK
		  )
			log_msg("Error on channel close for %s", pm->parent->name);
		ssh_channel_free(pm->ch[i]->channel);
		free(pm->ch[i]);
		remove_fdmap(pm->parent->chan_sock_fdmap, pm->ch[i]->sock_fd);
	}
	remove_fdmap(pm->parent->listen_fdmap, pm->listen_fd);
	free(pm->ch);
	free(pm->remote_host);
	free(pm);
}

int connect_forward_channel(struct static_port_map *pm, struct chan_sock *cs)
{
	int rc;
	rc = ssh_channel_open_forward(cs->channel, pm->remote_host,
								  pm->remote_port, "localhost", pm->local_port);
	if(rc != SSH_OK)	{
		log_msg("Error: error opening forward %d -> %s:%d", pm->local_port,
				pm->remote_host, pm->remote_port);
		remove_channel_from_map(pm, cs);
		return -1;
	}
	return 0;
}

int remove_map_from_gw(struct gw_host *gw, struct static_port_map *map)
{
	int i;

	for(i = 0; i < gw->n_maps; i++)
		if(map == gw->pm[i])
			break;

	if(i == gw->n_maps)	{
		log_msg("Error: map %p not found in gw->pm (%p)", map, gw->pm);
		return 1;
	}

	if(close(map->listen_fd) < 0)
		log_exit_perror(-1, "Error closing listening fd=%d", map->listen_fd);

	if(gw->n_maps >= 1)	{
		while(i < gw->n_maps - 1)	{
			gw->pm[i] = gw->pm[i + 1];
			i++;
		}
		gw->n_maps--;
		free_map(map);
	}
	return 0;
}
