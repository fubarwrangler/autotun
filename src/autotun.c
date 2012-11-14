#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include "autotun.h"
#include "ssh.h"
#include "port_map.h"
#include "net.h"
#include "my_fdset.h"

int _debug = 0;
int _verbose = 0;
char *prog_name = "autotunnel";


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

int select_loop(struct gw_host *gw)
{

	fd_set master, read_fds;
	ssh_channel *channels, *outchannels;
	socket_t maxfd;
	my_fdset listen_set = new_fdset();
	int i, j;
	size_t n_chans = 0;

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
		n_chans += gw->pm[i]->n_channels;
	}
	channels = safemalloc(sizeof(ssh_channel) * (1 + n_chans),
						  "channels ptr-array");
	outchannels = safemalloc(sizeof(ssh_channel) * (1 + n_chans),
							 "outchannels ptr-array");

	for(i = 0; i < gw->n_maps; i++)	{
		for(j = 0; j < gw->pm[i]->n_channels; j++)
			channels[i] = gw->pm[i]->ch[j]->channel;
	}
	channels[n_chans] = NULL;


	while(1)	{
		struct timeval tm;

		tm.tv_sec = 1;
		tm.tv_usec = 0;
		read_fds = master;
		printf("read_fds: ");
		for(i = 0; i < FD_SETSIZE; i++)
			if(FD_ISSET(i, &read_fds))
				printf(" %d", i);
		putchar('\n');
		switch(ssh_select(channels, outchannels, maxfd + 1, &read_fds, &tm))
		{
			case SSH_EINTR:
				continue;
			case SSH_OK:
				break;
			default:
				log_exit(1, "ssh_select error");
		}
		/* On connect, create+add new channel to map */
		for(i = 0; i <= maxfd; i++)	{
			if(FD_ISSET(i, &read_fds))	{
				debug("Set %d", i);
				if(fd_in_set(listen_set, i))	{
					int new_fd = accept_connection(i);
					struct static_port_map *pm;
					ssh_channel channel;

					debug("is listen fd, new conn accepted(%d): fd=%d", i, new_fd);
					channel = ssh_channel_new(gw->session);
					if((pm = get_map_for_listening(gw, i)) == NULL)
						log_exit(1, "Error: fd %d map not found", i);
					add_channel_to_map(pm, channel, new_fd);
					FD_SET(new_fd, &master);
					if(new_fd > maxfd)
						maxfd = new_fd;
				} else {
					/* read n write n stuff here */
				}

			}
		}
		debug("select() loop");
		//sleep(1);

	}

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
	add_map_to_gw(gw, 2020, "farmweb01.domain.local", 80);

	select_loop(gw);

	destroy_gw(gw);
	return 0;
}
