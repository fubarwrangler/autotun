#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>


#include "util.h"

/**
 * Fill the @buf passed in with a human-readable IP-address of the @sa
 *
 * This should work on both IPv4 and IPv6 addresses, and will exit if the
 * passed-in buffer is not large enough.
 *
 * @buf	Buffer to fill with IP-addr string
 * @len	length of this buffer (must be at least 7)
 * @sa	The sockaddr struct to read from
 * @return	Nothing
 */
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

/**
 * Create a listening socket bound to interface given by @node
 *
 * @local_port	The listening socket with a pending connection (via select())
 * @node		Nodename to bind to (localhost)
 * @return		The newly created file-descriptor
 *
 * NOTE: Much taken from the ridiculously useful http://beej.us/guide/bgnet/
 */
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

	/* loop through all the results and bind to the first we can */
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

	freeaddrinfo(servinfo);

	if (p == NULL)
		log_exit(2, "Failed to bind an address!");

	if (listen(sockfd, 10) < 0)
		log_exit_perror(1, "listen new fd=%d", sockfd);

	return sockfd;
}

/**
 * Small wrapper around accept() for user-connected sockets
 *
 * @listenfd	The listening socket with a pending connection (via select())
 * @return		The newly created file-descriptor
 */
int accept_connection(int listenfd)
{
	struct sockaddr_storage their_addr;
	socklen_t addr_size;
	int new_fd;

    addr_size = sizeof their_addr;
	new_fd = accept(listenfd, (struct sockaddr *)&their_addr, &addr_size);
	if(new_fd < 0)
		log_exit_perror(1, "accept on socket fd=%d", listenfd);

	return new_fd;
}
