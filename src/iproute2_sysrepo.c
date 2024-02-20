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
 *               2024 Okda Networks, <contact@okdanetworks.com>
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

#include <sysrepo.h>
#include "lib/iproute2_cmdgen.h"

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

static sr_session_ctx_t *sr_session;
static sr_conn_ctx_t *sr_connection;
static sr_subscription_ctx_t *sr_sub_ctx;
static char *iproute2_ip_modules[] = { "iproute2-ip-link",
                                       "iproute2-ip-nexthop",
                                       NULL }; // null terminator


volatile int exit_application = 0;

static void sigint_handler(__attribute__((unused)) int signum)
{
    printf("Sigint called, exiting...\n");
    exit_application = 1;
}


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
    fprintf(stderr, "argument \"%s\" is unknown!\n"
                    "run with no arguments to start the sysrepo integration, "
                    "or use iproute2's arguments to execute command.\n", argv0);

    return EXIT_FAILURE;
}

int ip_sr_config_change_cb_prepare(const struct lyd_node *dnode)
{
    //TODO: add validation, and generate_argv() might be called here.
    return SR_ERR_OK;
}

int ip_sr_config_change_cb_apply(const struct lyd_node *change_dnode)
{
    int ret;
    struct cmd_args **ipr2_cmds;
    if (change_dnode == NULL) {
        return SR_ERR_INVAL_ARG;
    }

    ipr2_cmds = lyd2cmd_argv(change_dnode);

    for (int i = 0; ipr2_cmds[i] != NULL; i++) {
        ret = do_cmd(ipr2_cmds[i]->argv[1], ipr2_cmds[i]->argc - 1, ipr2_cmds[i]->argv + 1);
        if (ret != EXIT_SUCCESS)// TODO: add rollback functionality.
            return SR_ERR_INTERNAL;
    }
    return SR_ERR_OK;
}


int ip_sr_config_change_cb(sr_session_ctx_t *session, uint32_t sub_id,
                           const char *module_name, const char *xpath,
                           sr_event_t sr_ev, uint32_t request_id,
                           void *private_data)
{
    int ret;
    const struct lyd_node *dnode;
    switch (sr_ev) {
        case SR_EV_ENABLED:
        case SR_EV_CHANGE:
            dnode = sr_get_changes(session);
            ret = ip_sr_config_change_cb_prepare(dnode);
            break;
        case SR_EV_DONE:
            dnode = sr_get_change_diff(session);
            ret = ip_sr_config_change_cb_apply(dnode);
            break;
        case SR_EV_ABORT:
            // TODO: add abort event functionality.
        case SR_EV_RPC:
            // TODO: rpc support for iproute2 commands.
        case SR_EV_UPDATE:
            return 0;
        default:
            fprintf(stderr, "%s: unexpected sysrepo event: %u\n", __func__,
                    sr_ev);
            return SR_ERR_INTERNAL;
    }

    return ret;
}

static void sr_subscribe_config()
{
    char **ip_module = iproute2_ip_modules;
    int ret;
    while (*ip_module != NULL) {
        ret = sr_module_change_subscribe(sr_session, *ip_module, NULL,
                                         ip_sr_config_change_cb, NULL,
                                         0, SR_SUBSCR_DEFAULT,
                                         &sr_sub_ctx);
        if (ret != SR_ERR_OK)
            fprintf(stderr,
                    "%s: failed to subscribe to module (%s): %s\n",
                    __func__, *ip_module, sr_strerror(ret));
        else
            fprintf(stdout,
                    "%s: successfully subscribed to module (%s)\n",
                    __func__, *ip_module);

        ++ip_module;
    }
    /* loop until ctrl-c is pressed / SIGINT is received */
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    while (!exit_application) {
        sleep(1);
    }
}

int sysrepo_start(){
    int ret;
    ret = sr_connect(SR_CONN_DEFAULT, &sr_connection);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_connect(): %s\n", __func__,
                sr_strerror(ret));
        goto cleanup;
    }

    /* Start session. */
    ret = sr_session_start(sr_connection, SR_DS_RUNNING, &sr_session);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_session_start(): %s\n", __func__,
                sr_strerror(ret));
        goto cleanup;
    }
    sr_subscribe_config();

cleanup:
    if (sr_session)
        sr_session_stop(sr_session);
    if (sr_connection)
        sr_disconnect(sr_connection);
    return EXIT_SUCCESS;
}

int
main(int argc, char **argv) {
    if (argc == 1)
        return sysrepo_start();

    if (rtnl_open(&rth, 0) < 0)
        return EXIT_FAILURE;

    int ret = do_cmd(argv[1], argc - 1, argv + 1);
    rtnl_close(&rth);
    return ret;
}
