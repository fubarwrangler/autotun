#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include "autotun.h"
#include "config.h"
#include "port_map.h"
#include "util.h"


static void process_global_config(struct ini_section *sec)
{
	char *p;

	p = ini_get_section_value(sec, "log_file");
	if(p != NULL)	{
		if((debug_stream = fopen(p, "a")) == NULL)	{
			debug_stream = stderr;
			log_exit_perror(-1, "Error opening logfile '%s'", p);
		}
	}
}

/**
 * Read the configuration file and mark the first gateway section
 *
 * Parse the config-file and filter any options in the first anon-section
 * befoe leaving the first proper section in @first_sec.
 *
 * @filename	The config-file to read
 * @first_sec	Place to put pointer to first section corresponding to a gw
 * @returns		A pointer to the full ini-file structure
 */
struct ini_file *
read_configfile(const char *filename, struct ini_section **first_sec)
{
	struct ini_file *ini;
	int rv;

	debug("Reading config file: '%s'", filename);
	switch(rv = ini_read_file(filename, &ini))	{
		case INI_OK:
			break;
		default:
			log_msg("Error reading config-file '%s'", filename);
			log_exit(-1, "%s: %s", ini_error_string(rv), strerror(errno));
			break;
	}

	if(ini->first == NULL)
		log_exit(1, "Configuration file is not valid");

	if(strcmp(ini->first->name, "") == 0)	{
		process_global_config(ini->first);
		*first_sec = ini->first->next;
	} else
		*first_sec = ini->first;

	if(*first_sec == NULL)
		log_exit(-1, "Error, no valid configuration sections found");

	return ini;
}

static uint32_t get_port(char *str)
{
	unsigned long int n;
	char *p;

	errno = 0;
	n = strtoul(str, &p, 10);
	if(errno != 0 || *p != '\0')
		log_exit(1, "Error: invalid integer port found: %s", str);

	if(n > 65536)
		log_exit(1, "Error: integer port out of range (%lu) > 2^16", n);

	return (uint16_t)n;
}

static inline bool is_port(char *str)
{
	while(*str)
		if(!isdigit(*str++))
			return false;
	return true;
}

static void parse_host_line(char *str, char **host, uint32_t *port)
{
	char *p;

	if((p = strtok(str, ":")) == NULL)
		log_exit(1, "Error: invalid host line found: %s", str);
	*host = p;
	if((p = strtok(NULL, ":")) == NULL)
		log_exit(1, "Error: port not found: %s", str);
	*port = get_port(p);
	if(strtok(NULL, ":") != NULL)
		log_exit(1, "Error: superfluous data found in host line: %s", str);
}

/**
 * Parse the gateway-config section to extract all gw-wide options like
 * compression and others and populate the appropriate fields of @gw.
 */
static void update_gw_config(struct ini_section *sec, struct gw_host *gw)
{
	int err;

	/* Zero / false is default */
	gw->compression = ini_get_section_bool(sec, "compression", &err);
	gw->c_level = ini_get_section_int(sec, "compression_level", &err);
	if(gw->compression && err != INI_OK)
		gw->c_level = 5;
	else if(!gw->compression && err == INI_OK)
		log_exit(2, "CFG Error: cannot set compression_level without compression");
	if(gw->compression)
		debug("Compression enabled, level %d", gw->c_level);

	gw->strict_host_key = ini_get_section_bool(sec, "strict_host_key", &err);
	if(err != INI_OK)	gw->strict_host_key = true;
	if(!gw->strict_host_key)
		debug("WARNING: strict host key checking DISABLED!");
}

/**
 * Create a new ssh_session based on the gw structure's config-fields
 */
static void create_gw_session(struct gw_host *gw)
{
	int ssh_verbosity = (_verbose == 0) ? SSH_LOG_NOLOG : SSH_LOG_FUNCTIONS;
	int off = 0, on = 1;
	if((gw->session = ssh_new()) == NULL)
		log_exit(-1, "ssh_new(): Error creating ssh session");

	ssh_options_set(gw->session, SSH_OPTIONS_HOST, gw->name);
	ssh_options_set(gw->session, SSH_OPTIONS_LOG_VERBOSITY, &ssh_verbosity);
	ssh_options_set(gw->session, SSH_OPTIONS_SSH1, &off);

	if(gw->compression)	{
		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION, &on);
		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION_LEVEL, &gw->c_level);
	}
}

/**
 * Create a gw_host struct from information held in the config-file section
 *
 * @sec     The ini-file section to parse
 * @returns A newly-created, empty gw_host struct
 */
struct gw_host *process_section_to_gw(struct ini_section *sec)
{
	struct ini_kv_pair *kvp;
	struct gw_host *gw;

	assert(sec != NULL && sec->items != NULL);

	gw = create_gw(sec->name);
	update_gw_config(sec, gw);

	kvp = sec->items;
	while(kvp)	{
		if(is_port(kvp->key))	{
			char *host;
			uint32_t rp, lp;

			lp = get_port(kvp->key);
			parse_host_line(kvp->value, &host, &rp);

			add_map_to_gw(gw, lp, host, rp);
		}
		kvp = kvp->next;
	}
	create_gw_session(gw);

	return gw;
}

