#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/libssh.h>

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
	ssh_session *session;
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
//	gw->pm = NULL;
	return gw;
}

void connect_gateway(struct gw_host *gw)
{
	connect_ssh_session(gw->session, gw->name);
}

void add_map_to_gw(struct gw_host *gw, uint32_t local_port,
				   char *host, uint32_t remote_port)
{
	struct static_port_map *spm;

	spm = safemalloc(sizeof(struct static_port_map), "static_port_map alloc");
	spm->gw = gw->name;
	spm->local_port = local_port;
	spm->remote_host = safestrdup(host, "spm strdup hostname");
	spm->remote_port = remote_port;

	saferealloc(&gw->pm, (gw->n_maps + 1) * sizeof(spm), "gw->pm realloc");
	gw->pm[gw->n_maps++] = spm;
}


/*struct static_port_map spm = { "gateway.domain", 8111, "intranet.domain.local", 80, NULL};
struct gw_host testfwd = { "gateway.domain", NULL, 0, 1, &spm };*/

int main(int argc, char *argv[])
{
	struct gw_host *gw = create_gw("gateway.domain");

	add_map_to_gw(gw, 8111, "intranet.domain.local", 80);



	return 0;
}
