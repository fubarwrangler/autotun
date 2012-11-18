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
#include "config.h"
#include "port_map.h"
#include "ssh.h"

int _debug = 0;
int _verbose = 0;
char *prog_name = "autotunnel";
char *cfgfile = NULL;

static void end_main_loop_handler(int signum)
{
    signum += 1; /* To shut up compiler warning about unused arg */
    end_ssh_select_loop = 1;
}

/* Setup signal handler */
static void setup_signals(void)
{
    struct sigaction sigterm_action, sighup_action;
    sigset_t self;

    sigemptyset(&self);
    sigaddset(&self, SIGINT);
    sigaddset(&self, SIGTERM);
    sigterm_action.sa_handler = end_main_loop_handler;
    sigterm_action.sa_mask = self;
    sigterm_action.sa_flags = 0;

	/* TODO: handle re-entry into main loop on reconfig */
    sigemptyset(&self);
    sigaddset(&self, SIGHUP);
    sighup_action.sa_handler = end_main_loop_handler;
    sighup_action.sa_mask = self;
    sighup_action.sa_flags = 0;

    sigaction(SIGINT, &sigterm_action, NULL);
    sigaction(SIGTERM, &sigterm_action, NULL);

	sigaction(SIGHUP, &sighup_action, NULL);

}

struct gw_host *create_gw(const char *hostname)
{
	struct gw_host *gw = safemalloc(sizeof(struct gw_host), "gw_host struct");
	gw->name = safestrdup(hostname, "gw_host strdup");
	gw->n_maps = 0;
	gw->pm = safemalloc(sizeof(struct static_port_map *), "gw_host pm array");
	gw->listen_fdmap = new_fdmap();
	gw->chan_sock_fdmap = new_fdmap();
	return gw;
}

void create_gw_session(struct gw_host *gw)
{
	int ssh_verbosity = (_verbose == 0) ? SSH_LOG_NOLOG : SSH_LOG_FUNCTIONS;
	int off = 0, on = 1;
	if((gw->session = ssh_new()) == NULL)
		log_exit(-1, "ssh_new(): Error creating ssh session");

	ssh_options_set(gw->session, SSH_OPTIONS_HOST, gw->name);
	ssh_options_set(gw->session, SSH_OPTIONS_LOG_VERBOSITY, &ssh_verbosity);
	ssh_options_set(gw->session, SSH_OPTIONS_SSH1, &off);

	if(gw->compression)	{
		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION, &on);
		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION_LEVEL, &gw->c_level);
	}
}

void connect_gateway(struct gw_host *gw)
{
	create_gw_session(gw);
	connect_ssh_session(&gw->session);
	authenticate_ssh_session(gw->session);
}

void destroy_gw(struct gw_host *gw)
{
	for(int i = 0; i < gw->n_maps; i++)
		free_map(gw->pm[i]);

	end_ssh_session(gw->session);

	del_fdmap(gw->listen_fdmap);
	del_fdmap(gw->chan_sock_fdmap);
	free(gw->pm);
	free(gw->name);
	free(gw);
}

void parseopts(int argc, char *argv[])
{
	int c;

	while((c=getopt(argc, argv, "vdf:")) != -1)	{
		switch(c)	{
			case 'd':
				_debug = 1;
				break;
			case 'v':
				_verbose = 1;
				break;
			case 'f':
				cfgfile = safestrdup(optarg, "cfgfile optarg");
				break;
			default:
				exit(2);
		}
	}
	if(cfgfile == NULL)	{
		char *home = getenv("HOME");
		if(home == NULL)
			log_exit(3, "Error: $HOME not set for default config file");
		cfgfile = safemalloc(strlen(home) + 16, "malloc cfgfile");
		strcpy(cfgfile, home);
		strcat(cfgfile, "/.autotunrc");
	}
}

int main(int argc, char *argv[])
{
	struct gw_host *gw;
	struct ini_file *ini;
	struct ini_section *sec;

	parseopts(argc, argv);
	setup_signals();

	ini = read_configfile(cfgfile, &sec);
	free(cfgfile);

	gw = process_section_to_gw(sec);

	connect_gateway(gw);



/*
	gw = create_gw("gateway.domain");

	connect_gateway(gw);
	add_map_to_gw(gw, 8111, "farmweb01.domain.local", 80);
	add_map_to_gw(gw, 27017, "farmeval02.domain.local", 27017);
	add_map_to_gw(gw, 2020, "nagios02.domain.local", 80);
	add_map_to_gw(gw, 5050, "crsdb01.domain.local", 22);
*/
	select_loop(gw);

	destroy_gw(gw);
	ini_free_data(ini);
	return 0;
}
