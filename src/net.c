#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>


#include "util.h"


/* Fill @buf with human-readable IP address from sockaddr structure */
static void get_ipaddr(char *buf, size_t len, struct sockaddr *sa)
{
    const char *res;

    assert(len > 7);

    memset(buf, 0, len);

    switch(sa->sa_family)   {

        case AF_INET:
            res = inet_ntop(AF_INET, &((struct sockaddr_in *)sa)->sin_addr,
                            buf, len);
            break;
        case AF_INET6:
            res = inet_ntop(AF_INET6, &((struct sockaddr_in6 *)sa)->sin6_addr,
                            buf, len);
            break;
        default:
            strncpy(buf, "bad AF", len);
            res = buf;
            break;
    }
    if(res == NULL)
        log_exit_perror(-1, "inet_ntop");
}

/* Much taken from the ridiculously useful http://beej.us/guide/bgnet/ */
int create_listen_socket(uint32_t local_port, const char *node)
{
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	char pstr[32];
	int yes=1;
	int rv;

	snprintf(pstr, 31, "%d", local_port);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(node, pstr, &hints, &servinfo)) != 0)
		log_exit(-1, "getaddrinfo: %s", gai_strerror(rv));

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			debug("Cannot create socket dom: %d, type: %d",
				  p->ai_family, p->ai_socktype);
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
			log_exit_perror(1, "setsockopt for listen socket");

		get_ipaddr(pstr, sizeof(pstr), p->ai_addr);
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			debug("Cannot bind this address: %s", pstr);
			continue;
		}
		debug("Bound socket fd to %s:%d", pstr, local_port);
		break;
	}

	if (p == NULL)
		log_exit(2, "Failed to bind any address!");


	freeaddrinfo(servinfo);

	if (listen(sockfd, 10) == -1) {
		perror("listen");
		exit(1);
	}
	return sockfd;
}