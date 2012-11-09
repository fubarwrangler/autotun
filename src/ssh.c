#include <stdio.h>
#include <stdlib.h>
#include <libssh/libssh.h>

#include "util.h"

int _debug = 1;
char *prog_name = "autotunnel";

struct remote_forward {
	uint32_t local_port;
	char *remote_host;
	uint32_t remote_port;
	char *gw_host;
};

struct dynamic_forward {
	uint32_t local_port;
	char *gw_host;
};


struct remote_forward testfwd = {
	8111, "farmweb01.domain.local", 80, "gateway.domain"
};

//int ssh_verbosity = SSH_LOG_RARE;
int ssh_verbosity = SSH_LOG_NOLOG;

int connect_ssh_session(struct remote_forward rf, ssh_session *session)
{
	if((*session = ssh_new()) == NULL)
		log_exit(-1, "ssh_new(): Error creating ssh session");

	ssh_options_set(*session, SSH_OPTIONS_HOST, testfwd.gw_host);
	ssh_options_set(*session, SSH_OPTIONS_LOG_VERBOSITY, &ssh_verbosity);


	if(ssh_connect(*session) != SSH_OK)
		log_exit(-1, "Error connecting to host: %s", ssh_get_error(*session));

	switch(ssh_is_server_known(*session))	{
		case SSH_SERVER_KNOWN_OK:
		case SSH_SERVER_NOT_KNOWN:
		case SSH_SERVER_FILE_NOT_FOUND:
			break;
		default:
			log_exit(-1, "Unknown server: %s", testfwd.gw_host);
	}

	return 0;
}

int authenticate_ssh_session(ssh_session session)
{
	/* This will only work if you are running ssh-agent */
	switch(ssh_userauth_autopubkey(session, ""))	{
		case SSH_AUTH_SUCCESS:
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
	ssh_disconnect(session);
	ssh_free(session);
}

int main(int argc, char *argv[])
{
	ssh_session session;

	connect_ssh_session(testfwd, &session);
	authenticate_ssh_session(session);


	end_ssh_session(session);
	return 0;
}
