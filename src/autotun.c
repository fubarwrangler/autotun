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
	static int sigintcnt = 0;

	switch(signum)	{
		case SIGINT:
			finish_main_loop = true;
			sigintcnt++;
			break;
		case SIGTERM:
			hard_shutdown = true;
			break;
		case SIGALRM:
			got_timer = true;
			break;
		default:
			break;
	}

	if(sigintcnt > 1)
		hard_shutdown = true;
}

/* Setup signal handler */
static void setup_signals(void)
{
    struct sigaction sigterm_action, sighup_action;
    sigset_t self;

    sigemptyset(&self);
    sigaddset(&self, SIGINT);
    sigaddset(&self, SIGTERM);
    sigterm_action.sa_handler = signal_handler;
    sigterm_action.sa_mask = self;
    sigterm_action.sa_flags = 0;

	/* TODO: handle re-entry into main loop on reconfig */
    sigemptyset(&self);
    sigaddset(&self, SIGHUP);
    sighup_action.sa_handler = signal_handler;
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
	gw->noconn_timeout = -1;
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
	debug("Session connected to gw %s", gw->name);
	connect_ssh_session(&gw->session);
	authenticate_ssh_session(gw->session);
}

void disconnect_gateway(struct gw_host *gw)
{
	debug("%d maps on gw", gw->n_maps);
	for(int i = 0; i < gw->n_maps; i++)	{
		if(gw->pm[i]->n_channels > 0)
			log_exit(1, "Error: cannot disconnect gw %s, %d active channels found"
						"on map %p (%d)", gw->name, gw->pm[i]->n_channels, gw->pm[i], i);
		free_map(gw->pm[i]);
	}
	debug("WARNING: Gateway disconnected");
	ssh_disconnect(gw->session);
}

void destroy_gw(struct gw_host *gw)
{
	disconnect_gateway(gw);
	ssh_free(gw->session);

	del_fdmap(gw->listen_fdmap);
	del_fdmap(gw->chan_sock_fdmap);
	free(gw->pm);
	free(gw->name);
	free(gw);
}

int gw_graceful_close(struct gw_host *gw)
{


	return 1;
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

char *strrev(char *str)
{
	char c, *p, *q;

	p = q = str;
	while(*q++)
		;
	q-=2;
	do	{
		c = *p;
		*p = *q;
		*q = c;
	} while(++p < --q);
	return str;
}
char *print_bin(int x)
{
	static char buf[128];
	int y = 0;

	memset(buf, 0, sizeof(buf));
	do {
		buf[y++] = (x & 0x01) ? '1' : '0';
	} while(x >>= 1 && y < sizeof(buf) - 1);

	return strrev(buf);
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

	create_gw_session(gw);
	connect_gateway(gw);

	select_loop(gw);

	destroy_gw(gw);
	ini_free_data(ini);
	return 0;
}
