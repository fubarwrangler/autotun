#ifndef _PFLOCK_H_
#define _PFLOCK_H_

typedef void (*pflock_eventhandler)(pid_t, int);

#define PF_RUNNING 1
#define PF_EXITED  2
#define PF_KILLED  4

#define PFW_ERROR   -1
#define PFW_CHAVAIL -2
#define PFW_NOCHILD -3
#define PFW_NONBLK  -4
#define PFW_AGAIN   -5
#define PFW_REMOVED -6

struct pflock_proc {
	pid_t pid;
	int status;
	void *handle;
	struct pflock *parent;
	pflock_eventhandler local_evh[2];
};


struct pflock {
	int n_procs;
	pid_t pgid;
	struct pflock_proc **flock;
	pflock_eventhandler evh[2];
};


struct pflock *pflock_new(pflock_eventhandler exit, pflock_eventhandler kill);
int pflock_get_numrun(struct pflock *pf);
int pflock_remove(struct pflock_proc *proc);
struct pflock_proc *
pflock_fork_data_events(struct pflock *pf,
						void *data,
						pflock_eventhandler on_exit,
						pflock_eventhandler on_kill);
struct pflock_proc *
pflock_fork_event(struct pflock *pf,
				  pflock_eventhandler on_exit,
				  pflock_eventhandler on_kill);
struct pflock_proc *pflock_fork_data(struct pflock *pf, void *data);
struct pflock_proc *pflock_fork(struct pflock *pf);
int pflock_poll(struct pflock *pf);
int pflock_wait(struct pflock *pf);
int pflock_wait_remove(struct pflock *pf, int remove_mask);
void pflock_sendall(struct pflock *pf, int signum);
int pflock_destroy(struct pflock *pf);


#endif
