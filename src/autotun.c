#include "autotun.h"
#include "port_map.h"
#include "ssh.h"


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
void setup_signals_for_child(void)
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

void setup_signals_parent(void)
{
	setup_signals_for_child();
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

void destroy_gw(struct gw_host *gw)
{
	ssh_blocking_flush(gw->session, 10);

	while(gw->n_maps)
		remove_map_from_gw(gw->pm[0]);

	ssh_disconnect(gw->session);
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
