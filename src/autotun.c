#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <stddef.h>

#include "autotun.h"
#include "ssh.h"

int _debug = 0;
int _verbose = 0;
char *prog_name = "autotunnel";

static void safe_stop_handler(int signum)
{
    signum += 1; /* To shut up compiler warning about unused arg */
    end_ssh_select_loop = 1;
}


/* Setup signal handler */
static void setup_signals(void)
{
    struct sigaction new_action;
    sigset_t self;

    sigemptyset(&self);
    sigaddset(&self, SIGINT);
    sigaddset(&self, SIGTERM);
    new_action.sa_handler = safe_stop_handler;
    new_action.sa_mask = self;
    new_action.sa_flags = 0;

    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGTERM, &new_action, NULL);
}


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


void parseopts(int argc, char *argv[])
{
	int c;

	while((c=getopt(argc, argv, "vd")) != -1)	{
		switch(c)	{
			case 'd':
				_debug = 1;
				break;
			case 'v':
				_verbose = 1;
				break;
			default:
				exit(2);
		}
	}
}


int main(int argc, char *argv[])
{
	parseopts(argc, argv);

	struct gw_host *gw = create_gw("gateway.domain");

	connect_gateway(gw);
	add_map_to_gw(gw, 8111, "farmweb01.domain.local", 80);
	add_map_to_gw(gw, 27017, "farmeval02.domain.local", 27017);
	add_map_to_gw(gw, 2020, "nagios02.domain.local", 80);

/*	ssh_session *s = &gw->session;

	printf("Offset of session in gw: %ld\n", offsetof(struct gw_host, session));
	printf("gw: %p, s %p, gw->s: %p, gw thru of: %p\n", gw, s, &gw->session,
		((char *)(s) - offsetof(struct gw_host, session))
	);

	return 0; */

	setup_signals();
	select_loop(gw);

	destroy_gw(gw);
	return 0;
}
