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
#define PF_KILLED  4

#define PFW_ERROR   -1
#define PFW_CHAVAIL -2
#define PFW_NOCHILD -3
#define PFW_NONBLK  -4
#define PFW_REMOVED -5

struct pflock_proc {
	pid_t pid;
	int status;
	void *handle;
	struct pflock *parent;
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

volatile sig_atomic_t proc_sig = 0;

static void sig_handler(int signum)
{
	debug("Caught signal %d", signum);
	proc_sig = signum;
}

/* Setup signal handler */
static void setup_signals(void)
{
    struct sigaction sigterm_action;
    sigset_t self;

    sigemptyset(&self);
    sigaddset(&self, SIGINT);
    sigaddset(&self, SIGTERM);
    sigterm_action.sa_handler = sig_handler;
    sigterm_action.sa_mask = self;
    sigterm_action.sa_flags = 0;


    sigaction(SIGINT, &sigterm_action, NULL);
    sigaction(SIGTERM, &sigterm_action, NULL);
}

struct pflock *pflock_new(pflock_eventhandler exit, pflock_eventhandler kill)
{
	struct pflock *pf = safemalloc(sizeof(struct pflock), "new pflock");
	pf->n_procs = 0;
	pf->flock = safemalloc(sizeof(struct pflock_proc *), "init pf->flock");
	pf->pgid = 0;
	pf->handle_exit = exit;
	pf->handle_kill = kill;
	return pf;
}

static int pflock_get_numrun(struct pflock *pf)
{
	int n_run = 0;

	for(int i = 0; i < pf->n_procs; i++)	{
		n_run += (pf->flock[i]->status == PF_RUNNING) ? 1 : 0;
		debug("PFlock(%d): %d -- status %d", i, pf->flock[i]->pid, pf->flock[i]->status);
	}

	return n_run;
}

static int pflock_remove(struct pflock_proc *proc)
{
	struct pflock *pf = proc->parent;
	int idx;

	if(proc->status == PF_RUNNING)	{
		log_msg("Error: tried to remove running process from flock: %d", proc->pid);
		return -1;
	}

	for(idx = 0; idx < pf->n_procs; idx++)
		if(pf->flock[idx] == proc)
			break;

	if(idx == pf->n_procs)
		log_exit(-1, "BUG: proc not found in parent pflock structure");

	for(; idx < pf->n_procs - 1; idx++)
		pf->flock[idx] = pf->flock[idx + 1];

	saferealloc((void**)&pf->flock,
				(pf->n_procs) * sizeof(struct pflock_proc *),
				"pflock status grow");

	pf->n_procs--;

	free(proc);

	return 0;
}

/**
 * Fork a new process, add a new proc-struct to the parent
 *
 * Forks a child (if first, create a new process group), then adds a handle to
 * the new child in the parent's pflock-structure. User-supplied pointer @data
 * gets attached to that proc's structure for convienance of tracking it in
 * the parent
 *
 * @pf   the process-flock to add a child to
 * @data optional convienance pointer to help keep track of child-process
 */
struct pflock_proc *pflock_fork_data(struct pflock *pf, void *data)
{
	pid_t pid;
	int n_run;
	struct pflock_proc *new_proc;

	n_run = pflock_get_numrun(pf);

	switch(pid = fork())	{
		case -1:
			log_exit_perror(-1, "fork()");
		case 0:
			if(n_run == 0)
				setpgid(0, 0);
			else
				setpgid(0, pf->pgid);
			return NULL;
		default:
			break;
	} /* Parent continues here, child returns 0 above */

	setpgid(pid, pf->pgid);
	/* First child determines process group for the rest */
	if(n_run == 0)
		pf->pgid = pid;

	new_proc = safemalloc(sizeof(*new_proc), "new_proc");
	new_proc->pid = pid;
	new_proc->status = PF_RUNNING;
	new_proc->handle = data;
	new_proc->parent = pf;

	saferealloc((void**)&pf->flock,
				(pf->n_procs + 1) * sizeof(struct pflock_proc *),
				"pflock status grow");
	pf->flock[pf->n_procs] = new_proc;
	pf->n_procs++;

	return new_proc;
}

struct pflock_proc *pflock_fork(struct pflock *pf)
{
	return pflock_fork_data(pf, NULL);
}

static int pflock_poll(struct pflock *pf)
{
	siginfo_t sin;
	int rv;

	sin.si_pid = 0;
	rv = waitid(P_PGID, pf->pgid, &sin, WEXITED | WNOHANG | WNOWAIT);
	if(rv == 0 && sin.si_pid == 0)
		return PFW_NONBLK;
	else if(rv == 0)
		return PFW_CHAVAIL;

	return -1;
}

int pflock_wait(struct pflock *pf)
{
	siginfo_t sin;
	struct pflock_proc *p;
	int i;

	if(pflock_get_numrun(pf) == 0)	{
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

int pflock_wait_remove(struct pflock *pf, int remove_mask)
{
	struct pflock_proc *p;
	int i;

	switch(i = pflock_wait(pf))	{
		case PFW_ERROR:
		case PFW_NOCHILD:
		case PFW_NONBLK:
			return i;
		default:
			break;
	}
	p = pf->flock[i];

	if( (p->status == PF_EXITED && remove_mask & PF_EXITED) ||
		(p->status == PF_KILLED && remove_mask & PF_KILLED) )	{
		pflock_remove(p);
		return PFW_REMOVED;
	}

	return i;
}

int pflock_sendall(struct pflock *pf, int signum)
{
	killpg(pf->pgid, signum);
}

int pflock_destroy(struct pflock *pf)
{
	if(pflock_get_numrun(pf) > 0)	{
		log_msg("WARNING: cannot destroy a pflock with running children");
		return -1;
	}

	for(int i = 0; i < pf->n_procs; i++)
		free(pf->flock[i]);
	free(pf->flock);
	free(pf);

	return 0;
}

int main(int argc, char *argv[])
{
	char buf[64];
	int idx;
	struct pflock *pf;
	debug_stream = stdout;

	pf = pflock_new(&handle_exit, &handle_kill);

	for(int x = 0; x < 6; x++)	{
		if(pflock_fork(pf) == NULL)	{
			int sleep_time;
			srand((unsigned)time(NULL) ^ getpid());
			sprintf(buf, "pflock-child%d", getpid());
			prog_name = buf;
			sleep_time = rand() % 20;
			log_msg("Child process! %d - %d (sleeping %d)", getpid(), getpgid(0), sleep_time);
			sleep(sleep_time);
			log_msg("Child end!");
			return sleep_time & 0x0f;
		}
	}
	log_msg("Parent proc! %d - %d", getpid(), getpgid(0));

	do	{
		struct pflock_proc *p;
		idx = pflock_wait_remove(pf, PF_KILLED|PF_EXITED);
		debug("pflock_wait(): returned %d%s", idx,
			  (idx == PFW_REMOVED) ? ": Removed proc from flock" : "");
		if(proc_sig != 0)	{
			pflock_sendall(pf, proc_sig);
			proc_sig = 0;
		}


	} while(idx != PFW_NOCHILD && idx != PFW_ERROR);

	pflock_destroy(pf);

	return 0;
}
