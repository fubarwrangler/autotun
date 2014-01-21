#ifndef _CONFIG_SSH_H__
#define _CONFIG_SSH_H__

#include "iniread.h"

#include "autotun.h"

struct ini_file *
read_configfile(const char *filename, struct ini_section **sec);
struct gw_host *process_section_to_gw(struct ini_section *sec);

#endif
