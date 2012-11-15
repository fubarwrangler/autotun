#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>


#include "autotun.h"
#include "port_map.h"
#include "net.h"

int end_ssh_select_loop = 0;

typedef struct my_fd {
	int *fds;
	int n;
} *my_fdset;

static my_fdset new_fdset(void)
{
	my_fdset fds;
	fds = safemalloc(sizeof(*fds), "newfdset");
	fds->n = 0;
	fds->fds = NULL;
	return fds;
}

static int fd_in_set(my_fdset set, int fd)
{
	for(int i = 0; i < set->n; i++)
		if(fd == set->fds[i])
			return 1;
	return 0;
}

static int add_fd_to_set(my_fdset set, int fd)
{
	if(fd_in_set(set, fd))
		return -1;

	saferealloc((void **)&set->fds, (set->n + 1) * sizeof(set->fds), "add fdset");
	set->fds[set->n] = fd;
	set->n++;
	return 0;
}

static int remove_fd_from_set(my_fdset set, int fd)
{
	int i;

	for(i = 0; i < set->n; i++)	{
		if(set->fds[i] == fd)
			break;
	}
	if(i == set->n)
		return -1;
	for(; i < set->n; i++)
		set->fds[i] = set->fds[i + 1];
	saferealloc((void **)&set->fds, set->n * sizeof(set->fds), "rm fdset");
	set->n--;

	return 0;
}

static void destroy_fdset(my_fdset set)
{
	free(set->fds);
	free(set);
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

static struct chan_sock *
get_cs_for_channel(struct gw_host *gw, ssh_channel ch)
{
	for(int i = 0; i < gw->n_maps; i++)	{
		for(int j = 0; j < gw->pm[i]->n_channels; j++)	{
			if(ch == gw->pm[i]->ch[j]->channel)
				return gw->pm[i]->ch[j];
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
	//debug("Update channels: new_n = %d, old = %d", new_n, *nchan);

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

	while(end_ssh_select_loop == 0)	{
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
				end_ssh_select_loop = 1;
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

				if((cs = get_chan_for_fd(gw, i)) == NULL)
					log_exit(1, "Error: fd %d channel not found", i);
				debug("Read activity on fd=%d, chan_sock %p (%p)", i, cs, &cs->sock_fd);

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

				int n_written = 0;

				debug("Read %d bytes from channel %p: %s", n_read, ch, buf);

				cs = get_cs_for_channel(gw, ch);
				debug("this chan_sock: (%p) %p", ch, cs);
				while(n_written < n_read)	{
					int rc;
					rc = write(cs->sock_fd, buf, n_read);
					if(rc < 0)
						log_exit(1, "Write error");
					n_written += rc;
				}
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
