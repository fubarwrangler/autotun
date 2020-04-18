#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>


#include "util.h"
#include "port_map.h"
#include "autotun.h"
#include "net.h"

/**
 * Add a mapping (local port -> remote host + port) to the gateway structure.
 *
 * Creates a listening port for the local side and adds the fd to the fd_map
 * on the gateway that maps listening ports to the map structure.
 *
 * The mappings are stored in an array of pointers gw->pm that is grown
 * appropriately and gw->n_maps stores the size of this array.
 *
 * @gw			gateway structure to add to
 * @local_port	the local port to listen on -- bound to localhost:NNNN
 * @host		the remote host to tunnel to
 * @remote_port	the port on the remote side to connect to
 * @return		Nothing, if anything fails here the program exits
 */
void add_map_to_gw(struct gw_host *gw,
				  uint32_t local_port,
				  char *host,
				  uint32_t remote_port)
{
	struct static_port_map *spm;

	debug("Adding map %d %s:%d to %s", local_port, host, remote_port, gw->name);

	spm = safemalloc(sizeof(struct static_port_map), "static_port_map alloc");
	spm->parent = gw;
	spm->local_port = local_port;
	spm->remote_host = safestrdup(host, "spm strdup hostname");
	spm->remote_port = remote_port;
	spm->ch = safemalloc(sizeof(struct chan_sock *), "spm->ch");

	spm->listen_fd = create_listen_socket(local_port, gw->local ? "localhost" : "*");
	add_fdmap(gw->listen_fdmap, spm->listen_fd, spm);
	spm->parent = gw;
	spm->n_channels = 0;

	saferealloc((void **)&gw->pm, (gw->n_maps + 1) * sizeof(spm), "gw->pm realloc");
	gw->pm[gw->n_maps++] = spm;
}

/**
 * Add a new channel to a specific remote-host mapping
 *
 * Creates a new channel in the mapping and returns a pointer to it, updating
 * the fd_map in the gateway (pm->parent) that maps connection socket fd to
 * the new channel structure.
 *
 * @pm		mapping to add a new channel to
 * @channel	newly created ssh_channel
 * @sock_fd	socket that the client is connected on
 * @return	Pointer to newly created channel struct
 */
struct chan_sock *
add_channel_to_map(struct static_port_map *pm,
				   ssh_channel channel,
				   int sock_fd)
{
	struct chan_sock *cs = safemalloc(sizeof(struct chan_sock), "add ch cs");

	debug("Adding channel %p to map %s:%d", channel, pm->remote_host, pm->remote_port);
	saferealloc((void **)&pm->ch, (pm->n_channels + 1) * sizeof(pm->ch),
				"pm->channel realloc");
	cs->channel = channel;
	cs->sock_fd = sock_fd;
	add_fdmap(pm->parent->chan_sock_fdmap, sock_fd, cs);
	pm->ch[pm->n_channels] = cs;
	pm->n_channels++;
	cs->parent = pm;
	return cs;
}

/**
 * Remove a channel from its associated port mapping structure
 *
 * Take the channel given and remove it from its parent port_map structure,
 * closing it first and removing it from the fd_map in the gateway struct that
 * maps client sockets -> channels.
 *
 * All channel pointers in pm->ch[] higher than this one are shifted down by
 * one position and the array is shrunk appropriately.
 *
 * @cs		channel structure to remove
 * @return	Nothing, if there are errors here they are either logged or the
 *          program exits (on malloc failure)
 */
void remove_channel_from_map(struct chan_sock *cs)
{
	struct static_port_map *pm = cs->parent;
	int i;

	if(cs->parent == NULL)
		log_exit(FATAL_ERROR, "Corrupt chan_sock parent %p->parent NULL", cs);

	for(i = 0; i < pm->n_channels; i++)	{
		if(pm->ch[i] == cs)	{
			if( ssh_channel_is_open(cs->channel) &&
				ssh_channel_close(cs->channel) != SSH_OK)
					log_msg("Error on channel close for %s", pm->parent->name);
			ssh_channel_free(cs->channel);
			break;
		}
	}

	debug("Destroy channel %p, closing fd=%d", cs->channel, cs->sock_fd);
	for(; i < pm->n_channels - 1; i++)
		pm->ch[i] = pm->ch[i + 1];

	saferealloc((void **)&pm->ch, pm->n_channels * sizeof(cs),
				"pm->channel realloc");
	pm->n_channels -= 1;
	close(cs->sock_fd);

	/* Remove this fd from parent gw's fd_map */
	remove_fdmap(pm->parent->chan_sock_fdmap, cs->sock_fd);
	free(cs);
}

/**
 * Free the map structure and destroy all connected channels
 *
 * The removal of channels is done by remove_channel_from_map(), and then
 * the listenening file-descriptor is closed and removed from the fd_map
 *
 * @pm		the map to destroy
 * @return	Nothing, any errors encountered are fatal
 *
 */
static void free_map(struct static_port_map *pm)
{
	debug("Freeing map %p (listen on %d) %d channels", pm, pm->local_port, pm->n_channels);

	while(pm->n_channels)
		remove_channel_from_map(pm->ch[0]);

	remove_fdmap(pm->parent->listen_fdmap, pm->listen_fd);
	if(close(pm->listen_fd) < 0)
		log_msg("Error closing listening fd=%d: %s", pm->listen_fd,
				strerror(errno));
	free(pm->ch);
	free(pm->remote_host);
	free(pm);
}

/**
 * Open a libssh forwarding channel for the given channel socket/channel pair
 *
 * Try to open a forwarding channel and if it fails, remove the chan_sock
 * attribute from the pm.
 *
 * @cs		the map to destroy
 * @return	0 if OK, -1 on error
 */
int connect_forward_channel(struct chan_sock *cs)
{
	struct static_port_map *pm = cs->parent;
	int rc;
	rc = ssh_channel_open_forward(cs->channel, pm->remote_host,
								  pm->remote_port, "localhost", pm->local_port);
	if(rc != SSH_OK)	{
		log_msg("Error: error opening forward %d -> %s:%d", pm->local_port,
				pm->remote_host, pm->remote_port);
		remove_channel_from_map(cs);
		return -1;
	}
	return 0;
}

/**
 * Remove a mapping from the gateway
 *
 * All channel pointers in gw->pm[] higher than this one are shifted down by
 * one position and the array is shrunk appropriately. The map is freed, which
 * closes all channels that may still be associated with it.
 *
 * @map		The map to delete
 * @return	Nothing, anything that goes wrong exits the program
 */
void remove_map_from_gw(struct static_port_map *map)
{
	struct gw_host *gw = map->parent;
	int i;

	for(i = 0; i < gw->n_maps; i++)
		if(map == gw->pm[i])
			break;

	if(i == gw->n_maps)
		log_exit(FATAL_ERROR, "Error: map %p not found in gw->pm (%p)",
				 map, gw->pm);

	if(gw->n_maps >= 1)	{
		while(i < gw->n_maps - 1)	{
			gw->pm[i] = gw->pm[i + 1];
			i++;
		}
		gw->n_maps--;
		free_map(map);
	}
}
