#ifndef _SSH_H__
#define _SSH_H__

#include <libssh/libssh.h>
#include "autotun.h"

void connect_ssh_session();
void authenticate_ssh_session(ssh_session session);
void end_ssh_session(ssh_session session);


#endif
