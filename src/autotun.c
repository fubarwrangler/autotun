#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autotun.h"
#include "ssh.h"

int _debug = 1;
char *prog_name = "autotunnel";


struct static_port_map {
	char *gw; /* Redundant but must be same as gw_host->name */
	uint32_t local_port;
	char *remote_host;
	uint32_t remote_port;
	ssh_channel channel;
};

struct gw_host {
	char *name;
	ssh_session session;
	int status;
	int n_maps;
	struct static_port_map **pm;
};


struct gw_host *create_gw(char *hostname)
{
	struct gw_host *gw = safemalloc(sizeof(struct gw_host), "gw_host struct");
	gw->name = safestrdup(hostname, "gw_host strdup");
	gw->n_maps = 0;
	gw->pm = safemalloc(sizeof(struct static_port_map *), "gw_host pm[0]");
	//gw->pm = NULL;
	return gw;
}

void connect_gateway(struct gw_host *gw)
{
	connect_ssh_session(&gw->session, gw->name);
	authenticate_ssh_session(gw->session);
}

int add_map_to_gw(struct gw_host *gw, uint32_t local_port,
				   char *host, uint32_t remote_port)
{
	struct static_port_map *spm;

	spm = safemalloc(sizeof(struct static_port_map), "static_port_map alloc");
	spm->gw = gw->name;
	spm->local_port = local_port;
	spm->remote_host = safestrdup(host, "spm strdup hostname");
	spm->remote_port = remote_port;

	if((spm->channel = ssh_channel_new(gw->session)) == NULL)	{
		log_msg("Error: cannot open new ssh-channel on %s -> %s connection",
				gw->name, host);
		return -1;
	}

	saferealloc((void **)&gw->pm, (gw->n_maps + 1) * sizeof(spm), "gw->pm realloc");
	gw->pm[gw->n_maps++] = spm;

	return 0;
}

void free_map(struct static_port_map *pm)
{
	if(ssh_channel_close(pm->channel) != SSH_OK)
		log_msg("Error on channel close for %s", pm->gw);
	ssh_channel_free(pm->channel);
	free(pm->remote_host);
	free(pm);
}


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

/*void print_maps(struct gw_host *gw)
{
	for(int i = 0; i < gw->n_maps; i++)	{
		printf("Map %d: %d local -> %s:%d\n", i, gw->pm[i]->local_port,
				gw->pm[i]->remote_host, gw->pm[i]->remote_port);
	}
}*/

int main(int argc, char *argv[])
{
	struct gw_host *gw = create_gw("gateway.domain");

	connect_gateway(gw);
	add_map_to_gw(gw, 8111, "intranet.domain.local", 80);
	add_map_to_gw(gw, 27017, "farmeval02.domain.local", 27017);
	add_map_to_gw(gw, 2020, "farmweb01.domain.local", 3306);
	add_map_to_gw(gw, 8096, "hobbes.domain.local", 22);


	return 0;
}
