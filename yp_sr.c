/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Vincent Jardin, <vjardin@free.fr>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU Affero General Public
 *              License Version 3.0 as published by the Free Software Foundation;
 *              either version 3.0 of the License, or (at your option) any later
 *              version.
 *
 * Copyright (C) 2024 Vincent Jardin, <vjardin@free.fr>
 */

/* system includes */
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>

#include "utils.h"
#include "ip_common.h"

int preferred_family = AF_UNSPEC;
int human_readable;
int use_iec;
int show_stats;
int show_details;
int oneline;
int brief;
int json;
int timestamp;
int echo_request;
int force;
int max_flush_loops = 10;
int batch_mode;
bool do_all;

struct rtnl_handle rth = { .fd = -1 };

const char *get_ip_lib_dir(void)
{
	abort();
}

int
main(int argc, char argv[]) {
	return do_ipaddr(0, NULL);
}
