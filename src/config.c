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
			log_exit(CONFIG_ERROR, "%s: %s", ini_error_string(rv), strerror(errno));
			break;
	}

	if(ini->first == NULL)
		log_exit(CONFIG_ERROR, "Configuration file is not valid");

	if(strcmp(ini->first->name, "") == 0)	{
		process_global_config(ini->first);
		*first_sec = ini->first->next;
	} else
		*first_sec = ini->first;

	if(*first_sec == NULL)
		log_exit(CONFIG_ERROR, "Error, no valid configuration sections found");

	return ini;
}

static uint32_t get_port(char *str)
{
	unsigned long int n;
	char *p;

	errno = 0;
	n = strtoul(str, &p, 10);
	if(errno != 0 || *p != '\0')
		log_exit(CONFIG_ERROR, "Error: invalid integer port found: %s", str);

	if(n > 65536)
		log_exit(CONFIG_ERROR, "Error: integer port out of range (%lu) > 2^16", n);

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
		log_exit(CONFIG_ERROR, "Error: invalid host line found: %s", str);
	*host = p;
	if((p = strtok(NULL, ":")) == NULL)
		log_exit(CONFIG_ERROR, "Error: port not found: %s", str);
	*port = get_port(p);
	if(strtok(NULL, ":") != NULL)
		log_exit(CONFIG_ERROR, "Error: superfluous data found in host line: %s", str);
}

/**
 * Create a new ssh_session and set config based on the ini-section passed
 *
 * @sec	The ini-section containing pertinant config-information
 * @gw	gw_host struct to activate a new ssh_session for
 * @return None, any errors here or mis-configuration is considered fatal
 */
static void create_gw_session_config(struct ini_section *sec, struct gw_host *gw)
{
	int ssh_verbosity = (_verbose == 0) ? SSH_LOG_NOLOG : SSH_LOG_PROTOCOL;
	int err, off = 0, on = 1;
	char *str;
	bool compression;
	int c_level;

	if((gw->session = ssh_new()) == NULL)
		log_exit(CONNECTION_ERROR, "ssh_new(): Error creating ssh session");

	ssh_options_set(gw->session, SSH_OPTIONS_HOST, gw->name);
	ssh_options_set(gw->session, SSH_OPTIONS_LOG_VERBOSITY, &ssh_verbosity);
	ssh_options_set(gw->session, SSH_OPTIONS_SSH1, &off);

	/* Zero / false is default */
	compression = ini_get_section_bool(sec, "compression", &err);
	c_level = ini_get_section_int(sec, "compression_level", &err);
	if(compression)	{
		if(c_level <= 0 || err != INI_OK)
			c_level = 5;
		debug("Compression enabled, level %d", c_level);

		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION, "on");
		ssh_options_set(gw->session, SSH_OPTIONS_COMPRESSION_LEVEL, &c_level);
	} else if(c_level != 0 && !compression)	{
		log_exit(CONFIG_ERROR, "Error: cannot set compression-level if compression is not set");
	}

	if(!ini_get_section_bool(sec, "strict_host_key", &err) && err == INI_OK)	{
		debug("WARNING: strict host key checking DISABLED!");
		ssh_options_set(gw->session, SSH_OPTIONS_STRICTHOSTKEYCHECK, &off);
	} else {
		ssh_options_set(gw->session, SSH_OPTIONS_STRICTHOSTKEYCHECK, &on);
	}

	if((str = ini_get_section_value(sec, "proxy_command")) != NULL)
		ssh_options_set(gw->session, SSH_OPTIONS_PROXYCOMMAND, str);
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
	create_gw_session_config(sec, gw);

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

	return gw;
}

