#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssh.h"

void connect_ssh_session(ssh_session session)
{
	if(ssh_connect(session) != SSH_OK)
		log_exit(CONNECTION_ERROR, "Error connecting to host: %s", ssh_get_error(session));

	switch(ssh_session_is_known_server(session))	{
		case SSH_KNOWN_HOSTS_OK:
		case SSH_KNOWN_HOSTS_UNKNOWN:
		case SSH_KNOWN_HOSTS_NOT_FOUND:
			break;
		case SSH_KNOWN_HOSTS_ERROR:
			log_exit(CONNECTION_ERROR, "SSH Server error with session: %s",
					 ssh_get_error(session));
		default:
			log_exit(CONNECTION_RETRY, "Unknown error validating server");
	}
}

static void pubkey_auth(ssh_session session, const char *privkey)
{
	char pubkey[strlen(privkey) + 5];
	ssh_key k_pub, k_priv;
	
	sprintf(pubkey, "%s.pub", privkey);
	if(ssh_pki_import_pubkey_file(pubkey, &k_pub) != SSH_OK)
		log_exit(CONNECTION_ERROR, "Import public key %s: %s", 
				 pubkey, ssh_get_error(session));
	if(ssh_userauth_try_publickey(session, NULL, k_pub) != SSH_AUTH_SUCCESS)
		log_exit(CONNECTION_ERROR, "Public key %s rejected", pubkey);
	if(ssh_pki_import_privkey_file(privkey, NULL, NULL, NULL, &k_priv) != SSH_OK)
		log_exit(CONNECTION_ERROR, "Import private key %s: %s", 
				 privkey, ssh_get_error(session));
	if(ssh_userauth_publickey(session, NULL, k_priv) != SSH_AUTH_SUCCESS)
		log_exit(CONNECTION_ERROR, "Connecting with private key %s: %s", 
				 privkey, ssh_get_error(session));
	
	ssh_key_free(k_pub);
	ssh_key_free(k_priv);
}

void authenticate_ssh_session(ssh_session session, const char *key)
{
	if(key != NULL)
		pubkey_auth(session, key);
	else	{
		/* This will only work if you are running ssh-agent */
		switch(ssh_userauth_agent(session, NULL))	{
			case SSH_AUTH_SUCCESS:
				break;
			case SSH_AUTH_ERROR:
				log_exit(CONNECTION_ERROR, "Error occured authenticating: %s",
						ssh_get_error(session));
			default:
				log_exit(CONNECTION_ERROR, "Error: Not authenticated to server");
		}
	}
}

void end_ssh_session(ssh_session session)
{
	if(ssh_is_connected(session))
		ssh_disconnect(session);
	ssh_free(session);
}
