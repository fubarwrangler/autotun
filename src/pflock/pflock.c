#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "util.h"

char *prog_name = "pflock";
int _debug=0;

typedef void (*pflock_eventhandler)(pid_t, int, void *);

#define PF_RUNNING 1
#define PF_EXITED  2
#define PF_KILLED  3
#define PF_NOCHILD 4


struct pflock_proc {
	pid_t pid;
	int status;
	void *proc_data;
};


struct pflock {
	int n_procs;
	pid_t pgid;
	struct pflock_proc **flock;
	pflock_eventhandler handle_kill;
	pflock_eventhandler handle_exit;
};

void handle_exit(pid_t pid, int code, void *data)
{
	log_msg("Exit happened on pid %d: code %d", pid, code);
}
void handle_kill(pid_t pid, int signal, void *data)
{
	log_msg("Kill happened on pid %d: signal was %d", pid, signal);
}

struct pflock *pflock_new(pflock_eventhandler exit, pflock_eventhandler kill)
{
	struct pflock *pf = safemalloc(sizeof(struct pflock), "new pflock");
	pf->n_procs = 0;
	pf->flock = safemalloc(sizeof(struct pflock_proc *), "init pf->flock");
	pf->pgid = getpgid(0);
	pf->handle_exit = exit;
	pf->handle_kill = kill;
	return pf;
}

static int pflock_remove(struct pflock *pf, int idx)
{
	struct pflock_proc *proc = pf->flock[i];

	if(proc->status == PF_RUNNING)	{
		log_msg("Error: tried to remove running process from flock: %d", proc->pid);
		return -1;
	}
	while(idx++ < pf->n_procs - 1)
		pf->flock[i] = pf->flock[i + 1]

}

int pflock_fork(struct pflock *pf, void *assoc_data)
{
	pid_t pid;
	struct pflock_proc *new_proc;

	switch(pid = fork())	{
		case -1:
			log_exit_perror(-1, "fork()");
		case 0:
			setpgid(0, pf->pgid);
			return 0;
		default:
			break;
	} /* Parent continues here, child returns 0 above */

	new_proc = safemalloc(sizeof(*new_proc), "new_proc");
	new_proc->pid = pid;
	new_proc->status = PF_RUNNING;
	new_proc->proc_data = assoc_data;


	saferealloc((void**)&pf->flock,
				(pf->n_procs + 1) * sizeof(struct pflock_proc *),
				"pflock status grow");
	pf->flock[pf->n_procs] = new_proc;
	pf->n_procs++;
	return pid;
}

int pflock_wait(struct pflock *pf)
{
	siginfo_t sin;
	struct pflock_proc *p;
	int i;

	if(waitid(P_PGID, pf->pgid, &sin, WEXITED) != 0)	{
		log_exit_perror(-1, "waitid()");
	}
	for(i = 0; i < pf->n_procs; i++)	{
		if(sin.si_pid == pf->flock[i]->pid)
			break;
	}
	if(i == pf->n_procs)
		log_msg("ERROR: pid %d not found in process-flock!", sin.si_pid);
	p = pf->flock[i];

	switch(sin.si_code)	{
		case CLD_EXITED:
			p->status = PF_EXITED;
			if(pf->handle_exit != NULL)
				pf->handle_exit(p->pid, sin.si_status, p->proc_data);
			return PF_EXITED;
		case CLD_KILLED:
		case CLD_DUMPED:
			p->status = PF_KILLED;
			if(pf->handle_kill != NULL)
				pf->handle_kill(p->pid, sin.si_status, p->proc_data);
			return PF_KILLED;
		default:
			log_msg("Unknown code for waitid(): %d", sin.si_code);
			break;
	}
	return -1;
}

int main(int argc, char *argv[])
{

	struct pflock *pf;
	debug_stream = stdout;

	pf = pflock_new(&handle_exit, &handle_kill);

	for(int x = 0; x < 10; x++)	{


	if(pflock_fork(pf, NULL) == 0)	{
		prog_name = "pflock-child1";
		log_msg("Child process! %d - %d", getpid(), getpgid(0));
		sleep(122);
		log_msg("Child end!");
		return 12;
	} else	{
		pflock_wait(pf);
		log_exit(1, "Parent proc! %d - %d", getpid(), getpgid(0));
	}


	return 0;
}
