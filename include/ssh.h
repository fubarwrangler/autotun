#ifndef _SSH_H__
#define _SSH_H__

#include <libssh/libssh.h>

int connect_ssh_session(ssh_session *session, char *host);
int authenticate_ssh_session(ssh_session session);
void end_ssh_session(ssh_session session);

int select_loop(struct gw_host *gw);


#endif
