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
	int idx;

	debug_stream = stderr;

	parseopts(argc, argv);

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
			pflock_destroy(proc_per_gw);
			prog_name = safemalloc(64, "new progname");
			snprintf(prog_name, 63, "autotun-%s", sec->name);
			debug("New child process pid %d", getpid());

			setup_signals_for_child();
			gw = process_section_to_gw(sec);
			ini_free_data(ini);

			connect_ssh_session(&gw->session);
			authenticate_ssh_session(gw->session);
			return run_gateway(gw);
		}
		sec = sec->next;
	}

	setup_signals_parent();

	debug("Signal children GO");
	pflock_sendall(proc_per_gw, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &bmask, NULL);


	do	{
		idx = pflock_wait_remove(proc_per_gw, PF_KILLED);
		debug("pflock_wait(): returned %d%s", idx,
			  (idx == PFW_REMOVED) ? ": Removed proc from flock" : "");
		if(finish_main_loop != 0)	{
			debug("Sending %s to all", !hard_shutdown ? "SIGINT" : "SIGTERM");
			pflock_sendall(proc_per_gw, hard_shutdown ? SIGTERM : SIGINT );
			finish_main_loop = 0;
		}
	} while(idx != PFW_NOCHILD && idx != PFW_ERROR && !hard_shutdown);

	if(pflock_get_numrun(proc_per_gw) > 0)
		pflock_sendall(proc_per_gw, SIGTERM);

	ini_free_data(ini);
	pflock_destroy(proc_per_gw);
	return 0;
}

