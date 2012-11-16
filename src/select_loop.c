#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>


#include "autotun.h"
#include "port_map.h"
#include "net.h"

int end_ssh_select_loop = 0;

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

int new_connection(struct gw_host *gw,
				   int listenfd,
				   fd_set *listen_set,
				   fd_set *master_set)
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

		remove_map_from_gw(gw, pm);
		FD_CLR(listenfd, listen_set);
		FD_CLR(listenfd, master_set);

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

#define CHAN_BUF_SIZE 1024

int select_loop(struct gw_host *gw)
{

	fd_set master, read_fds, listen_set;
	ssh_channel *channels = NULL, *outchannels = NULL;
	socket_t maxfd = 0;
	char buf[CHAN_BUF_SIZE];
	int i, j, n_chans = 0;

	FD_ZERO(&master);
	FD_ZERO(&listen_set);
	for(i = 0; i < gw->n_maps; i++)	{
		FD_SET(gw->pm[i]->listen_fd, &master);
		if(gw->pm[i]->listen_fd > maxfd)
			maxfd = gw->pm[i]->listen_fd;
		for(j = 0; j < gw->pm[i]->n_channels; j++)	{
			if(gw->pm[i]->ch[j]->sock_fd > maxfd)
				maxfd = gw->pm[i]->ch[j]->sock_fd;
			FD_SET(gw->pm[i]->ch[j]->sock_fd, &master);
		}
		FD_SET(gw->pm[i]->listen_fd, &listen_set);
	}

	/* This is the program's main loop right here */
	while(end_ssh_select_loop == 0)	{
		struct timeval tm;
		int n_read;
		struct chan_sock *cs;

		tm.tv_sec = 2;
		tm.tv_usec = 500000;
		read_fds = master;
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
		/* Loop over our custom select'd fd's to see if there are any new
		 * connections or reads waiting to happen and perform them
		 */
		for(i = 0; i <= maxfd; i++)	{

			if(!FD_ISSET(i, &read_fds))
				continue;

			/* On connect, create+add new channel to map */
			if(FD_ISSET(i, &listen_set))	{
				int new_fd = new_connection(gw, i, &listen_set, &master);
				if(new_fd >= 0)	{
					FD_SET(new_fd, &master);
					if(new_fd > maxfd)
						maxfd = new_fd;
				}
				continue;
			}

			/* Otherwise read data from socket and write to channel */
			if((cs = get_chan_for_fd(gw, i)) == NULL)
				log_exit(1, "Error: fd %d channel not found", i);
			n_read = read(cs->sock_fd, buf, sizeof(buf));

			debug("Read activity on user socket fd=%d: %d bytes", i, n_read);

			if(n_read < 0)	{
				log_exit_perror(1, "Read error on fd=%d channel %p", i, cs->channel);
			} else if(n_read == 0)	{
			/* Tear down the channel on zero-read if user disconnected */
				struct static_port_map *pm;

				debug("Read 0 bytes on fd=%d, closing channel %p", i, cs->channel);

				if((pm = get_map_for_channel(gw, cs)) == NULL)
					log_exit(1, "Error: map not found for channel");

				remove_channel_from_map(pm, cs);

				FD_CLR(i, &master);
			} else {
			/* Otherwise pass user data to ssh_channel */
				int n_written = 0;
				while(n_written < n_read)	{
					int rv;
					//if(ssh_channel_is_eof(cs->channel))
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
		/* Read any output from ssh and pass it to the client sockets */
		for(i = 0; outchannels[i] != NULL; i++)	{
			ssh_channel ch = outchannels[i];

			n_read = ssh_channel_read(ch, buf, sizeof(buf), 0);

			if(n_read > 0)	{

				int n_written = 0;

				debug("Read %d bytes from channel %p", n_read, ch);
				cs = get_cs_for_channel(gw, ch);

				while(n_written < n_read)	{
					int rc;
					rc = write(cs->sock_fd, buf, n_read);
					if(rc < 0)	{
						log_exit_perror(1, "Write error on socket %d: %s");
						break;
					}
					n_written += rc;
				}
			} else if (n_read == 0)	{
				/* close socket */
				debug("BUG!: Zero bytes read from channel %p", ch);
			} else {
				/* error case */
				debug("Error on channel %p", ch);
			}
		}
	}

	debug("Exiting main loop...");
	free(channels);
	free(outchannels);
	return 0;
}