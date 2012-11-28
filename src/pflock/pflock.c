#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "util.h"

char *prog_name = "pflock";
int _debug=0;

typedef void (*pflock_eventhandler)(pid_t, uint32_t);

#define PF_EXITED 0x100
#define PF_KILLED 0x200
#define PF_STPSTT 0x400

struct pflock {
	int n_procs;
	int *status;
	pid_t *pids;
	pid_t pgid;
	pflock_eventhandler event;
};

void handle_exit(pid_t pid, uint32_t etype)
{
	log_msg("%d happened on pid %d", etype, pid);
}

struct pflock *new_pflock(void)
{
	struct pflock *pf = safemalloc(sizeof(struct pflock), "new pflock");
	pf->n_procs = 0;
	pf->status = NULL;
	pf->pids = NULL;
	pf->event = NULL;
	pf->pgid = getpgid(0);
	return pf;
}

int pflock_fork(struct pflock *pf)
{
	pid_t pid;

	switch(pid = fork())	{
		case -1:
			log_exit_perror(-1, "fork()");
		case 0:
			setpgid(0, pf->pgid);
			return 0;
		default:
			break;
	} /* Parent continues here, child returns 0 above */

	saferealloc((void**)&pf->status, (pf->n_procs + 1) * sizeof(int), "pflock status grow");
	saferealloc((void**)&pf->pids, (pf->n_procs + 1) * sizeof(pid_t), "pflock pids grow");
	pf->status[pf->n_procs] = 0;
	pf->pids[pf->n_procs] = pid;
	pf->n_procs++;
	return pid;
}

int pflock_wait(struct pflock *pf)
{
	siginfo_t sin;
	int i;

	if(waitid(P_PGID, pf->pgid, &sin, WEXITED) != 0)	{
		log_exit_perror(-1, "waitid()");
	}
	for(i = 0; i < pf->n_procs; i++)	{
		if(sin.si_pid == pf->pids[i])
			break;
	}
	if(i == pf->n_procs)
		log_msg("ERROR: pid %d not found in process-flock!", sin.si_pid);
	else
		psiginfo(&sin, "lol");
		pf->event(sin.si_pid, sin.si_status);
}



int main(int argc, char *argv[])
{

	struct pflock *pf;
	debug_stream = stdout;

	pf = new_pflock();
	pf->event = &handle_exit;

	if(pflock_fork(pf) == 0)	{
		prog_name = "pflock-child1";
		log_msg("Child process! %d - %d", getpid(), getpgid(0));
		sleep(10);
		log_msg("Child end!");
		return 1;
	} else	{
		pflock_wait(pf);
		log_exit(1, "Parent proc! %d - %d", getpid(), getpgid(0));
	}


	return 0;
}
