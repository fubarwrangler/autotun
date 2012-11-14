#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <stddef.h>

#include "autotun.h"
#include "ssh.h"
#include "port_map.h"
#include "net.h"
#include "my_fdset.h"

int _debug = 0;
int _verbose = 0;
char *prog_name = "autotunnel";

#define container_of(ptr, type, member) ({ \
			const typeof( ((type *)0)->member ) *__mptr = (ptr); \
			(type *)( (char *)__mptr - offsetof(type,member) );})

#define get_cs_for_channel(c) container_of(c, struct chan_sock, channel)

bool shutdown_main_loop = false;


static void safe_stop_handler(int signum)
{
    signum += 1; /* To shut up compiler warning about unused arg */
    shutdown_main_loop = true;
}


/* Setup signal handler */
static void setup_signals(void)
{
    struct sigaction new_action;
    sigset_t self;

    sigemptyset(&self);
    sigaddset(&self, SIGINT);
    sigaddset(&self, SIGTERM);
    new_action.sa_handler = safe_stop_handler;
    new_action.sa_mask = self;
    new_action.sa_flags = 0;

    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGTERM, &new_action, NULL);
}


struct gw_host *create_gw(char *hostname)
{
	struct gw_host *gw = safemalloc(sizeof(struct gw_host), "gw_host struct");
	gw->name = safestrdup(hostname, "gw_host strdup");
	gw->n_maps = 0;
	gw->pm = safemalloc(sizeof(struct static_port_map *), "gw_host pm array");
	return gw;
}

void connect_gateway(struct gw_host *gw)
{
	connect_ssh_session(&gw->session, gw->name);
	authenticate_ssh_session(gw->session);
}

void destroy_gw(struct gw_host *gw)
{
	for(int i = 0; i < gw->n_maps; i++)
		free_map(gw->pm[i]);
	free(gw->pm);
	free(gw->name);
	end_ssh_session(gw->session);
	free(gw);
}


static struct static_port_map *
get_map_for_listening(struct gw_host *gw, int listen_fd)
{
	for(int i = 0; i < gw->n_maps; i++)	{
		if(gw->pm[i]->listen_fd == listen_fd)
			return gw->pm[i];
	}
	return NULL;
}

static struct chan_sock *
get_chan_for_fd(struct gw_host *gw, int fd)
{
	for(int i = 0; i < gw->n_maps; i++)	{
		for(int j = 0; j < gw->pm[i]->n_channels; j++)	{
			if(fd == gw->pm[i]->ch[j]->sock_fd)
				return gw->pm[i]->ch[j];
		}
	}
	return NULL;
}

static struct static_port_map *
get_map_for_channel(struct gw_host *gw, struct chan_sock *cs)
{
	for(int i = 0; i < gw->n_maps; i++)	{
		for(int j = 0; j < gw->pm[i]->n_channels; j++)	{
			if(cs == gw->pm[i]->ch[j])
				return gw->pm[i];
		}
	}
	return NULL;
}

int new_connection(struct gw_host *gw, int listenfd)
{
	struct static_port_map *pm;
	struct chan_sock *cs;
	ssh_channel channel;
	int new_fd;

	new_fd = accept_connection(listenfd);

	debug("is listen fd, new conn accepted(%d): fd=%d", listenfd, new_fd);

	if((channel = ssh_channel_new(gw->session)) == NULL)
		log_exit(1, "Error creating new channel for connection");

	if((pm = get_map_for_listening(gw, listenfd)) == NULL)
		log_exit(1, "Error: fd %d map not found", listenfd);

	cs = add_channel_to_map(pm, channel, new_fd);

	if(connect_forward_channel(pm, cs) < 0)	{
		log_msg("Removing listening port %d because bad connection", pm->local_port);

		remove_channel_from_map(pm, cs);

		return -1;
	}
	return new_fd;
}

static int update_channels(struct gw_host *gw,
						   ssh_channel **chs,
						   ssh_channel **outchs,
						   int *nchan)
{
	int i, j, k;
	int new_n = 0;

	for(i = 0; i < gw->n_maps; i++)
		new_n += gw->pm[i]->n_channels;
	debug("Update channels: new_n = %d, old = %d", new_n, *nchan);

	if(new_n == *nchan && new_n > 0)
		return 0;

	saferealloc((void **)chs, (new_n + 1) * sizeof(ssh_channel), "channels");
	saferealloc((void **)outchs, (new_n + 1) * sizeof(ssh_channel), "outchannels");

	for(i = 0, k = 0; i < gw->n_maps; i++)	{
		for(j = 0; j < gw->pm[i]->n_channels; j++)
			(*chs)[k++] = gw->pm[i]->ch[j]->channel;
	}
	(*chs)[new_n] = NULL;
	*nchan = new_n;

	return 1;
}

int select_loop(struct gw_host *gw)
{

	fd_set master, read_fds;
	ssh_channel *channels = NULL, *outchannels = NULL;
	socket_t maxfd = 0;
	my_fdset listen_set = new_fdset();
	int i, j;
	int n_chans = 0;

	FD_ZERO(&master);
	for(i = 0; i < gw->n_maps; i++)	{
		FD_SET(gw->pm[i]->listen_fd, &master);
		if(gw->pm[i]->listen_fd > maxfd)
			maxfd = gw->pm[i]->listen_fd;
		for(j = 0; j < gw->pm[i]->n_channels; j++)	{
			if(gw->pm[i]->ch[j]->sock_fd > maxfd)
				maxfd = gw->pm[i]->ch[j]->sock_fd;
			FD_SET(gw->pm[i]->ch[j]->sock_fd, &master);
		}
		add_fd_to_set(listen_set, gw->pm[i]->listen_fd);
	}

	while(!shutdown_main_loop)	{
		struct timeval tm;
		char buf[1024];

		tm.tv_sec = 1;
		tm.tv_usec = 0;
		read_fds = master;
		printf("read_fds: ");
		for(i = 0; i < FD_SETSIZE; i++)
			if(FD_ISSET(i, &read_fds))
				printf(" %d", i);
		putchar('\n');
		update_channels(gw, &channels, &outchannels, &n_chans);
		switch(ssh_select(channels, outchannels, maxfd + 1, &read_fds, &tm))
		{
			case SSH_EINTR:
				debug("select() gave EINTR");
				continue;
			case SSH_OK:
				break;
			default:
				log_msg("ssh_select error reported!");
				shutdown_main_loop = true;
		}
		for(i = 0; i <= maxfd; i++)	{
			struct chan_sock *cs;

			if(!FD_ISSET(i, &read_fds))
				continue;

			/* On connect, create+add new channel to map */
			if(fd_in_set(listen_set, i))	{
				int new_fd = new_connection(gw, i);
				if(new_fd >= 0)	{
					FD_SET(new_fd, &master);
					if(new_fd > maxfd)
						maxfd = new_fd;
				}
			/* Otherwise read data from socket and write to channel */
			} else {
				int n_read;

				debug("Read activity on fd=%d", i);
				if((cs = get_chan_for_fd(gw, i)) == NULL)
					log_exit(1, "Error: fd %d channel not found", i);

				n_read = read(cs->sock_fd, buf, sizeof(buf));
				if(n_read < 0)	{
					log_exit(1, "Read error on fd=%d channel %p", i, cs->channel);
				} else if(n_read == 0)	{
				/* Tear down the channel on zero-read if user disconnected */
					struct static_port_map *pm;

					debug("Read 0 bytes on fd=%d, closing channel %p", i, cs->channel);

					if((pm = get_map_for_channel(gw, cs)) == NULL)
						log_exit(1, "Error: map not found for channel");

					remove_channel_from_map(pm, cs);

					FD_CLR(i, &master);
				} else {
					int n_written = 0;
					printf("Read %d bytes on fd\n", n_read);
					while(n_written < n_read)	{
						int rv;

						rv = ssh_channel_write(cs->channel, buf + n_written,
												   n_read - n_written);
						if(rv == SSH_ERROR || ssh_channel_is_eof(cs->channel))	{
							log_msg("Error on ssh_write to channel %p: %s",
									cs->channel, ssh_get_error(cs->channel));

							/* Should we shut down this way? */
							if(shutdown(cs->sock_fd, SHUT_WR) != 0)
								log_exit_perror(1, "shutdown socket %d", i);
							break;
						}
						n_written += rv;
					}
				}
			}
		}
		/* Read output from ssh and pass it to the client sockets */
		for(i = 0; outchannels[i] != NULL; i++)	{
			int n_read;
			ssh_channel ch = outchannels[i];
			n_read = ssh_channel_read(ch, buf, sizeof(buf), 0);
			if(n_read > 0)	{
				struct chan_sock *cs;
				cs = get_cs_for_channel(ch);
				int n_written = 0;
				while(n_written < n_read)	{
					int rc;
					rc = write(cs->sock_fd, buf, n_read);
					if(rc < 0)
						log_exit(1, "Write error");
					n_written += rc;
				}

				debug("Read %d bytes from channel %p: %s", n_read, ch, buf);
			} else if (n_read == 0)	{
				/* close socket */
				debug("Zero bytes read from channel %p", ch);
			} else {
				/* error case */
				debug("Error on channel %p", ch);
			}
		}
	}

	debug("Exiting main loop...");

	destroy_fdset(listen_set);
	free(channels);
	free(outchannels);
	return 0;
}


void parseopts(int argc, char *argv[])
{
	int c;

	while((c=getopt(argc, argv, "vd")) != -1)	{
		switch(c)	{
			case 'd':
				_debug = 1;
				break;
			case 'v':
				_verbose = 1;
				break;
			default:
				exit(2);
		}
	}
}


int main(int argc, char *argv[])
{
	parseopts(argc, argv);

	struct gw_host *gw = create_gw("gateway.domain");

	connect_gateway(gw);
	add_map_to_gw(gw, 8111, "intranet.domain.local", 80);
	add_map_to_gw(gw, 27017, "farmeval02.domain.local", 27017);
	add_map_to_gw(gw, 2020, "nagios02.domain.local", 80);

	setup_signals();
	select_loop(gw);

	destroy_gw(gw);
	return 0;
}
