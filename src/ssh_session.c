#include <stdio.h>
#include <stdlib.h>

#include "ssh.h"

int connect_ssh_session(ssh_session *session)
{
	if(ssh_connect(*session) != SSH_OK)
		log_exit(-1, "Error connecting to host: %s", ssh_get_error(*session));

	switch(ssh_is_server_known(*session))	{
		case SSH_SERVER_KNOWN_OK:
		case SSH_SERVER_NOT_KNOWN:
		case SSH_SERVER_FILE_NOT_FOUND:
			break;
		default:
			log_exit(-1, "Unknown ssh server");
	}

	return 0;
}

int authenticate_ssh_session(ssh_session session)
{
	/* This will only work if you are running ssh-agent */
	switch(ssh_userauth_autopubkey(session, NULL))	{
		case SSH_AUTH_SUCCESS:
			debug("New session authenticated");
			return 0;
		case SSH_AUTH_ERROR:
			log_exit(-1, "Error occured authenticating: %s",
					 ssh_get_error(session));
		default:
			log_exit(-1, "Error: Not authenticated to server");
	}
}

void end_ssh_session(ssh_session session)
{
	if(ssh_is_connected(session))
		ssh_disconnect(session);
	ssh_free(session);
}

const char *disconnect_reason(ssh_session session)
{
	const char *p;

	if(ssh_is_connected(session))
		return NULL;
	if((p = ssh_get_disconnect_message(session)) == NULL)
		p = ssh_get_error(session);
	return p;
}
