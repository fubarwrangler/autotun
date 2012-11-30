#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "util.h"

char *prog_name = "pflock";
int _debug=1;

typedef void (*pflock_eventhandler)(pid_t, int);

#define PF_RUNNING 1
#define PF_EXITED  2
#define PF_KILLED  3

#define PFW_ERROR   -1
#define PFW_NOCHILD -2
#define PFW_NONBLK  -3

struct pflock_proc {
	pid_t pid;
	int status;
};


struct pflock {
	int n_procs;
	pid_t pgid;
	struct pflock_proc **flock;
	pflock_eventhandler handle_kill;
	pflock_eventhandler handle_exit;
};

void handle_exit(pid_t pid, int code)
{
	log_msg("Exit happened on pid %d: code %d", pid, code);
}
void handle_kill(pid_t pid, int signal)
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
	struct pflock_proc *proc = pf->flock[idx];

	if(proc->status == PF_RUNNING)	{
		log_msg("Error: tried to remove running process from flock: %d", proc->pid);
		return -1;
	}

	for(; idx < pf->n_procs - 1; idx++)
		pf->flock[idx] = pf->flock[idx + 1];

	saferealloc((void**)&pf->flock,
				(pf->n_procs) * sizeof(struct pflock_proc *),
				"pflock status grow");

	pf->n_procs--;

	free(proc);
}

int pflock_fork(struct pflock *pf)
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
	int n_run = 0;
	int i;

	for(i = 0; i < pf->n_procs; i++)	{
		n_run += (pf->flock[i]->status == PF_RUNNING) ? 1 : 0;
		debug("PFlock(%d): %d -- status %d", i, pf->flock[i]->pid, pf->flock[i]->status);
	}

	if(n_run == 0)	{
		debug("INFO: pflock_wait() called with no running children");
		return PFW_NOCHILD;
	}

	if(waitid(P_PGID, pf->pgid, &sin, WEXITED) != 0)	{
		log_exit_perror(-1, "waitid()");
	}

	for(i = 0; i < pf->n_procs; i++)	{
		if(sin.si_pid == pf->flock[i]->pid)
			break;
	}
	if(i == pf->n_procs)	{
		log_msg("ERROR: pid %d not found in process-flock!", sin.si_pid);
		return PFW_ERROR;
	}

	p = pf->flock[i];

	switch(sin.si_code)	{
		case CLD_EXITED:
			p->status = PF_EXITED;
			if(pf->handle_exit != NULL)
				pf->handle_exit(p->pid, sin.si_status);
			return i;
		case CLD_KILLED:
		case CLD_DUMPED:
			p->status = PF_KILLED;
			if(pf->handle_kill != NULL)
				pf->handle_kill(p->pid, sin.si_status);
			return i;
		default:
			log_msg("Unknown code for waitid(): %d", sin.si_code);
			break;
	}
	return PFW_ERROR;
}

int main(int argc, char *argv[])
{
	char buf[64];
	int p;
	struct pflock *pf;
	debug_stream = stdout;

	pf = pflock_new(&handle_exit, &handle_kill);

	for(int x = 0; x < 6; x++)	{
		if(pflock_fork(pf) == 0)	{
			int sleep_time;
			srand((unsigned)time(NULL) ^ getpid());
			sprintf(buf, "pflock-child%d", getpid());
			prog_name = buf;
			sleep_time = rand() % 16;
			log_msg("Child process! %d - %d (sleeping %d)", getpid(), getpgid(0), sleep_time);
			sleep(sleep_time);
			log_msg("Child end!");
			return 12;
		}
	}

	do	{
		p = pflock_wait(pf);
		debug("pflock_wait(): returned %d", p);
		if(p >= 0)
			pflock_remove(pf, p);
	} while(p != PFW_NOCHILD && p != PFW_ERROR);
	log_exit(1, "Parent proc! %d - %d", getpid(), getpgid(0));

	return 0;
}
