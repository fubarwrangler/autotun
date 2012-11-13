#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autotun.h"
#include "ssh.h"
#include "port_map.h"

int _debug = 1;
char *prog_name = "autotunnel";


struct gw_host *create_gw(char *hostname)
{
	struct gw_host *gw = safemalloc(sizeof(struct gw_host), "gw_host struct");
	gw->name = safestrdup(hostname, "gw_host strdup");
	gw->n_maps = 0;
	gw->pm = safemalloc(sizeof(struct static_port_map *), "gw_host pm array");
	return gw;
}

void connect_gateway(struct gw_host *gw)
{
	connect_ssh_session(&gw->session, gw->name);
	authenticate_ssh_session(gw->session);
}


void destroy_gw(struct gw_host *gw)
{
	for(int i = 0; i < gw->n_maps; i++)
		free_map(gw->pm[i]);
	free(gw->pm);
	free(gw->name);
	end_ssh_session(gw->session);
	free(gw);
}

int main(int argc, char *argv[])
{
	struct gw_host *gw = create_gw("gateway.domain");

	connect_gateway(gw);
	add_map_to_gw(gw, 8111, "intranet.domain.local", 80);
	add_map_to_gw(gw, 27017, "farmeval02.domain.local", 27017);
	add_map_to_gw(gw, 2020, "farmweb01.domain.local", 3306);
	add_map_to_gw(gw, 8096, "hobbes.domain.local", 22);


	destroy_gw(gw);
	return 0;
}
