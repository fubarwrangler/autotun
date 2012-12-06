#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <stddef.h>

#include "autotun.h"
#include "pflock.h"
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

void connect_gateway(struct gw_host *gw)
{
	connect_ssh_session(&gw->session);
	authenticate_ssh_session(gw->session);
}

void disconnect_gateway(struct gw_host *gw)
{
	ssh_blocking_flush(gw->session, 10);

	while(gw->n_maps)
		remove_map_from_gw(gw->pm[0]);

	ssh_disconnect(gw->session);
}

void destroy_gw(struct gw_host *gw)
{
	disconnect_gateway(gw);
	ssh_free(gw->session);

	del_fdmap(gw->listen_fdmap);
	del_fdmap(gw->chan_sock_fdmap);
	free(gw->name);
	free(gw->pm);
	free(gw);
}

int run_gateway(struct gw_host *gw)
{
	siginfo_t si;
	struct timespec tm = {6, 0};
	sigset_t ss;
	int code = 0;


    sigemptyset(&ss);
    sigaddset(&ss, SIGUSR1);
    sigaddset(&ss, SIGTERM);

	debug("Waiting for go/no-go signal");

	do {
		if((code = sigtimedwait(&ss, &si, &tm)) < 0)	{
			if(errno == EINTR)
				continue;
			else if(errno == EAGAIN)
				log_exit(NO_ERROR, "Signal not recieved in timeout, parent dead?");
		}
	} while(code < 0);

	if(si.si_pid != getppid())
		log_exit(FATAL_ERROR, "Non parent-proc %d sent signal %d", si.si_pid, code);

	if(code == SIGTERM)
		log_exit(NO_ERROR, "Cancel signal recieved, exiting cleanly");
	else if(code == SIGUSR1)
		debug("Go signal recieved, entering select loop");
	else
		log_exit(FATAL_ERROR, "Wrong signal recieved: %d", code);

	sigprocmask(SIG_UNBLOCK, &ss, NULL);

	select_loop(gw);
	destroy_gw(gw);
	ssh_finalize();
	return 0;
}

char *default_cfg = ".autotunrc";

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
		cfgfile = safemalloc(strlen(home) + strlen(default_cfg) + 3, "malloc cfgfile");
		sprintf(cfgfile, "%s/%s", home, default_cfg);
	}
}

struct pflock *proc_per_gw;

void exit_cleanup(void)
{
	int num;

	if(proc_per_gw != NULL)	{

		if((num = pflock_get_numrun(proc_per_gw)) == 0)
			return;

		debug("atexit(): send SIGTERM to pflock (%d running)", num);
		pflock_sendall(proc_per_gw, SIGTERM);
	}
}

int main(int argc, char *argv[])
{
	struct gw_host *gw;
	struct ini_file *ini;
	struct ini_section *sec;
	sigset_t bmask;
	bool child = false;
	int idx;

	debug_stream = stderr;

	parseopts(argc, argv);
	setup_signals();

	ini = read_configfile(cfgfile, &sec);
	free(cfgfile);

	proc_per_gw = pflock_new(NULL, NULL);

	sigemptyset(&bmask);
	sigaddset(&bmask ,SIGUSR1);
	sigaddset(&bmask ,SIGTERM);
	if(sigprocmask(SIG_BLOCK, &bmask, NULL) < 0)
		log_exit_perror(FATAL_ERROR, "sigprocmask() blocking setup");

	while(sec)	{
		if(pflock_fork_data(proc_per_gw, sec) == NULL)	{
			prog_name = safemalloc(64, "new progname");
			snprintf(prog_name, 63, "autotun-%s", sec->name);
			debug("New child process pid %d", getpid());

			child = true;
			break;
		}
		sec = sec->next;
	}


	if(child)	{
		gw = process_section_to_gw(sec);
		ini_free_data(ini);
		connect_gateway(gw);
		return run_gateway(gw);
	}

	atexit(exit_cleanup);

	debug("Signal children GO");
	pflock_sendall(proc_per_gw, SIGUSR1);

	do	{
		idx = pflock_wait_remove(proc_per_gw, PF_KILLED);
		debug("pflock_wait(): returned %d%s", idx,
			  (idx == PFW_REMOVED) ? ": Removed proc from flock" : "");
		if(finish_main_loop != 0)	{
			debug("Sending SIGINT to all");
			pflock_sendall(proc_per_gw, SIGINT);
			finish_main_loop = 0;
		}
	} while(idx != PFW_NOCHILD && idx != PFW_ERROR && !hard_shutdown);

	if(pflock_get_numrun(proc_per_gw) > 0)
		pflock_sendall(proc_per_gw, SIGTERM);

	ini_free_data(ini);
	return 0;
}
