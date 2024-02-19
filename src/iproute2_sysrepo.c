/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Vincent Jardin, <vjardin@free.fr>
 *              Ali Aqrabawi, <aaqrabaw@okdanetworks.com>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU Affero General Public
 *              License Version 3.0 as published by the Free Software Foundation;
 *              either version 3.0 of the License, or (at your option) any later
 *              version.
 *
 * Copyright (C) 2024 Vincent Jardin, <vjardin@free.fr>
 *               2024 okda netrworks, <contact@okdanetworks.com>
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

#ifndef LIBDIR
#define LIBDIR "/usr/lib"
#endif

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
    const char *lib_dir;

    lib_dir = getenv("IP_LIB_DIR");
    if (!lib_dir)
        lib_dir = LIBDIR "/ip";

    return lib_dir;
}

static const struct cmd {
    const char *cmd;
    int (*func)(int argc, char **argv);
} cmds[] = {
        { "address",	do_ipaddr },
        { "addrlabel",	do_ipaddrlabel },
        { "maddress",	do_multiaddr },
        { "route",	do_iproute },
        { "rule",	do_iprule },
        { "neighbor",	do_ipneigh },
        { "neighbour",	do_ipneigh },
        { "ntable",	do_ipntable },
        { "ntbl",	do_ipntable },
        { "link",	do_iplink },
        { "l2tp",	do_ipl2tp },
        { "fou",	do_ipfou },
        { "ila",	do_ipila },
        { "macsec",	do_ipmacsec },
        { "tunnel",	do_iptunnel },
        { "tunl",	do_iptunnel },
        { "tuntap",	do_iptuntap },
        { "tap",	do_iptuntap },
        { "token",	do_iptoken },
        { "tcpmetrics",	do_tcp_metrics },
        { "tcp_metrics", do_tcp_metrics },
        { "monitor",	do_ipmonitor },
        { "xfrm",	do_xfrm },
        { "mroute",	do_multiroute },
        { "mrule",	do_multirule },
        { "netns",	do_netns },
        { "netconf",	do_ipnetconf },
        { "vrf",	do_ipvrf},
        { "sr",		do_seg6 },
        { "nexthop",	do_ipnh },
        { "mptcp",	do_mptcp },
        { "ioam",	do_ioam6 },
        { "stats",	do_ipstats },
        { 0 }
};

static int do_cmd(const char *argv0, int argc, char **argv)
{
    const struct cmd *c;

    for (c = cmds; c->cmd; ++c) {
        if (matches(argv0, c->cmd) == 0)
            return -(c->func(argc - 1, argv + 1));
    }

    return EXIT_FAILURE;
}

int
main(int argc, char **argv) {
    if (rtnl_open(&rth, 0) < 0)
        return EXIT_FAILURE;

    int ret = do_cmd(argv[1], argc - 1, argv + 1);
    rtnl_close(&rth);
    return ret;
}
