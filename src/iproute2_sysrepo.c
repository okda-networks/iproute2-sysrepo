/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Vincent Jardin, <vjardin@free.fr>
 *              Ali Aqrabawi, <aaqrabaw@okdanetworks.com>
 *              Amjad Daraiseh, <adaraiseh@okdanetworks.com>
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
#include <setjmp.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>

/* common iproute2 */
#include "utils.h"

/* ip module */
#include "ip_common.h"

/* tc module */
#include <dlfcn.h>
#include "tc_util.h"
#include "tc_common.h"

/* bridge module */
#include "br_common.h"

/* sysrepo */
#include "lib/cmdgen.h"
#include "lib/oper_data.h"
#include <sysrepo.h>

#ifndef LIBDIR
#define LIBDIR "/usr/lib"
#endif

/* ip definetions */
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

/* bridge definitions */
int compress_vlans;

/* tc definitions */
int show_raw;
int show_graph;
bool use_names;
static void *BODY; /* cached handle dlopen(NULL) */
static struct qdisc_util *qdisc_list;
static struct filter_util *filter_list;

/* common */
struct rtnl_handle rth = { .fd = -1 };

/* sysrepo */
sr_session_ctx_t *sr_session;
static sr_conn_ctx_t *sr_connection;
static sr_subscription_ctx_t *sr_sub_ctx;
/**
 * Struct tp store yang modules names and their operational data subscription path.
 */
struct yang_module {
    const char *module; // Stores Module name
    const char *oper_pull_path; // Stores operational pull subscription path
} ipr2_ip_modules[] = { { "iproute2-ip-link", "/iproute2-ip-link:links" },
                        { "iproute2-ip-nexthop", "/iproute2-ip-nexthop:nexthops" },
                        { "iproute2-ip-netns", "/iproute2-ip-netns:netnses" } };

volatile int exit_application = 0;
static jmp_buf jbuf;
static int jump_set = 0;

static void exit_cb(void)
{
    if (jump_set)
        longjmp(jbuf, EXIT_FAILURE);
}

static void sigint_handler(__attribute__((unused)) int signum)
{
    fprintf(stdout, "\nSigint called, exiting...\n");
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
} ip_cmds[] = { { "address", do_ipaddr },
                { "addrlabel", do_ipaddrlabel },
                { "maddress", do_multiaddr },
                { "route", do_iproute },
                { "rule", do_iprule },
                { "neighbor", do_ipneigh },
                { "neighbour", do_ipneigh },
                { "ntable", do_ipntable },
                { "ntbl", do_ipntable },
                { "link", do_iplink },
                { "l2tp", do_ipl2tp },
                { "fou", do_ipfou },
                { "ila", do_ipila },
                { "macsec", do_ipmacsec },
                { "tunnel", do_iptunnel },
                { "tunl", do_iptunnel },
                { "tuntap", do_iptuntap },
                { "tap", do_iptuntap },
                { "token", do_iptoken },
                { "tcpmetrics", do_tcp_metrics },
                { "tcp_metrics", do_tcp_metrics },
                { "monitor", do_ipmonitor },
                { "xfrm", do_xfrm },
                { "mroute", do_multiroute },
                { "mrule", do_multirule },
                { "netns", do_netns },
                { "netconf", do_ipnetconf },
                { "vrf", do_ipvrf },
                { "sr", do_seg6 },
                { "nexthop", do_ipnh },
                { "mptcp", do_mptcp },
                { "ioam", do_ioam6 },
                { "stats", do_ipstats },
                { 0 } },
  bridge_cmds[] = { { "link", do_link }, { "fdb", do_fdb }, { "mdb", do_mdb },
                    { "vlan", do_vlan }, { "vni", do_vni }, { 0 } },
  tc_cmds[] = { { "qdisc", do_qdisc }, { "class", do_class },    { "filter", do_filter },
                { "chain", do_chain }, { "actions", do_action }, { 0 } };

/* tc module main depenedenices */
static int print_noqopt(struct qdisc_util *qu, FILE *f, struct rtattr *opt)
{
    if (opt && RTA_PAYLOAD(opt))
        fprintf(f, "[Unknown qdisc, optlen=%u] ", (unsigned int)RTA_PAYLOAD(opt));
    return 0;
}

static int parse_noqopt(struct qdisc_util *qu, int argc, char **argv, struct nlmsghdr *n,
                        const char *dev)
{
    if (argc) {
        fprintf(stderr, "Unknown qdisc \"%s\", hence option \"%s\" is unparsable\n", qu->id, *argv);
        return -1;
    }
    return 0;
}

static int print_nofopt(struct filter_util *qu, FILE *f, struct rtattr *opt, __u32 fhandle)
{
    if (opt && RTA_PAYLOAD(opt))
        fprintf(f, "fh %08x [Unknown filter, optlen=%u] ", fhandle, (unsigned int)RTA_PAYLOAD(opt));
    else if (fhandle)
        fprintf(f, "fh %08x ", fhandle);
    return 0;
}

static int parse_nofopt(struct filter_util *qu, char *fhandle, int argc, char **argv,
                        struct nlmsghdr *n)
{
    __u32 handle;

    if (argc) {
        fprintf(stderr, "Unknown filter \"%s\", hence option \"%s\" is unparsable\n", qu->id,
                *argv);
        return -1;
    }
    if (fhandle) {
        struct tcmsg *t = NLMSG_DATA(n);

        if (get_u32(&handle, fhandle, 16)) {
            fprintf(stderr, "Unparsable filter ID \"%s\"\n", fhandle);
            return -1;
        }
        t->tcm_handle = handle;
    }
    return 0;
}

struct qdisc_util *get_qdisc_kind(const char *str)
{
    void *dlh;
    char buf[256];
    struct qdisc_util *q;

    for (q = qdisc_list; q; q = q->next)
        if (strcmp(q->id, str) == 0)
            return q;

    snprintf(buf, sizeof(buf), "%s/q_%s.so", get_tc_lib(), str);
    dlh = dlopen(buf, RTLD_LAZY);
    if (!dlh) {
        /* look in current binary, only open once */
        dlh = BODY;
        if (dlh == NULL) {
            dlh = BODY = dlopen(NULL, RTLD_LAZY);
            if (dlh == NULL)
                goto noexist;
        }
    }

    snprintf(buf, sizeof(buf), "%s_qdisc_util", str);
    q = dlsym(dlh, buf);
    if (q == NULL)
        goto noexist;

reg:
    q->next = qdisc_list;
    qdisc_list = q;
    return q;

noexist:
    q = calloc(1, sizeof(*q));
    if (q) {
        q->id = strdup(str);
        q->parse_qopt = parse_noqopt;
        q->print_qopt = print_noqopt;
        goto reg;
    }
    return q;
}

struct filter_util *get_filter_kind(const char *str)
{
    void *dlh;
    char buf[256];
    struct filter_util *q;

    for (q = filter_list; q; q = q->next)
        if (strcmp(q->id, str) == 0)
            return q;

    snprintf(buf, sizeof(buf), "%s/f_%s.so", get_tc_lib(), str);
    dlh = dlopen(buf, RTLD_LAZY);
    if (dlh == NULL) {
        dlh = BODY;
        if (dlh == NULL) {
            dlh = BODY = dlopen(NULL, RTLD_LAZY);
            if (dlh == NULL)
                goto noexist;
        }
    }

    snprintf(buf, sizeof(buf), "%s_filter_util", str);
    q = dlsym(dlh, buf);
    if (q == NULL)
        goto noexist;

reg:
    q->next = filter_list;
    filter_list = q;
    return q;
noexist:
    q = calloc(1, sizeof(*q));
    if (q) {
        strncpy(q->id, str, 15);
        q->parse_fopt = parse_nofopt;
        q->print_fopt = print_nofopt;
        goto reg;
    }
    return q;
}

static int do_cmd(int argc, char **argv)
{
    preferred_family = AF_UNSPEC;
    max_flush_loops = 10;
    const struct cmd *cmds;
    const struct cmd *c;
    if (argc < 2) {
        fprintf(stderr, "Missing arguments, 2 or more are needed.\n"
                        "\nPossible execution options:\n"
                        "1- Run with no arguments to start iproute2-sysrepo.\n"
                        "2- Run with individual iproute2 commands arguments.\n");
        return EXIT_FAILURE;
    }

    if (!strcmp(argv[0], "ip"))
        cmds = ip_cmds;
    else if (!strcmp(argv[0], "bridge"))
        cmds = bridge_cmds;
    else if (!strcmp(argv[0], "tc"))
        cmds = tc_cmds;
    else {
        fprintf(stderr,
                "Unknown argument \"%s\".\n"
                "\nPossible execution options:\n"
                "1- Run with no arguments to start iproute2-sysrepo.\n"
                "2- Run with individual iproute2 commands arguments.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    for (c = cmds; c->cmd; ++c) {
        if (matches(argv[1], c->cmd) == 0)
            return -(c->func(argc - 2, argv + 2));
    }

    fprintf(stderr,
            "Unknown argument \"%s\".\n"
            "\nPossible execution options:\n"
            "1- Run with no arguments to start iproute2-sysrepo.\n"
            "2- Run with individual iproute2 commands arguments.\n",
            argv[1]);

    return EXIT_FAILURE;
}

void print_cmd_line(int argc, char **argv)
{
    for (int k = 0; k < argc; k++) {
        fprintf(stdout, "%s ", argv[k]);
    }

    fprintf(stdout, "\n");
}

int ip_sr_config_change_cb_prepare(const struct lyd_node *dnode)
{
    //TODO: add validation, and generate_argv() might be called here.
    return SR_ERR_OK;
}

int ip_sr_config_change_cb_apply(const struct lyd_node *change_dnode)
{
    int ret = SR_ERR_OK;
    struct cmd_info **ipr2_cmds;
    if (change_dnode == NULL) {
        return SR_ERR_INVAL_ARG;
    }

    ipr2_cmds = lyd2cmds(change_dnode);
    if (ipr2_cmds == NULL) {
        fprintf(stderr, "%s: failed to generate commands for the change \n", __func__);
        return SR_ERR_CALLBACK_FAILED;
    }
    for (int i = 0; ipr2_cmds[i] != NULL; i++) {
        fprintf(stdout, "%s: executing command: ", __func__);
        print_cmd_line(ipr2_cmds[i]->argc, ipr2_cmds[i]->argv);
        jump_set = 1;
        if (setjmp(jbuf)) {
            // iproute2 exited, go to rollback.
            atexit(exit_cb);
            goto rollback;
        }
        ret = do_cmd(ipr2_cmds[i]->argc, ipr2_cmds[i]->argv);
        if (ret != EXIT_SUCCESS) {
rollback:
            fprintf(stderr, "%s: iproute2 command failed, cmd = ", __func__);
            print_cmd_line(ipr2_cmds[i]->argc, ipr2_cmds[i]->argv);
            // rollback on failure.
            for (i--; i >= 0; i--) {
                if (setjmp(jbuf)) {
                    // rollback cmd failed, continue with the reset rollback cmds.
                    atexit(exit_cb);
                    continue;
                }
                fprintf(stderr, "%s: executing rollback cmd: ", __func__);
                print_cmd_line(ipr2_cmds[i]->rollback_argc, ipr2_cmds[i]->rollback_argv);
                do_cmd(ipr2_cmds[i]->rollback_argc, ipr2_cmds[i]->rollback_argv);
            }
            ret = SR_ERR_CALLBACK_FAILED;
            break;
        }
    }
    free_cmds_info(ipr2_cmds);
    jump_set = 0;
    return ret;
}

/* TODO: move to a common lib file */
void free_argv2(char **argv, int argc)
{
    // Free memory for each string
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
        argv[i] = NULL;
    }
    // Free memory for the array of pointers
    free(argv);
}

int ip_sr_config_change_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name,
                           const char *xpath, sr_event_t sr_ev, uint32_t request_id,
                           void *private_data)
{
    sr_change_iter_t *it;
    int ret;
    struct lyd_node *root_dnode;
    const struct lyd_node *dnode;
    sr_change_oper_t sr_op;

    ret = sr_get_changes_iter(session, "//*", &it);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_get_changes_iter() failed for \"%s\"", __func__, module_name);
        return ret;
    }

    // we don't iterate through all changes, we just get the first changed node, then find it's root.
    // this root will have all the changes. NOTE: this approach will not work if the yang has more than
    // root containers. which is not the case in iproute2-yang modules.
    ret = sr_get_change_tree_next(session, it, &sr_op, &dnode, NULL, NULL, NULL);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: failed to get next change node: %s\n", __func__, sr_strerror(ret));
        return SR_ERR_INTERNAL;
    }

    root_dnode = (struct lyd_node *)dnode;
    while (root_dnode->parent != NULL) root_dnode = (struct lyd_node *)root_dnode->parent;

    // for each module changed sysrepo will call the change callback, which we don't want,
    // so we will get the last changed node in the list and apply the change only when the callback
    // module name equals to the last node, this will assure the changes will be applied once.
    struct lyd_node *next = NULL, *last_changed = NULL;
    LY_LIST_FOR(root_dnode, next)
    {
        last_changed = next;
    }
    switch (sr_ev) {
    case SR_EV_ENABLED:
    case SR_EV_CHANGE:
        // apply the changes at the last called callback.
        if (!strcmp(module_name, last_changed->schema->module->name)) {
            ret = ip_sr_config_change_cb_apply(root_dnode);
        }
        break;
    case SR_EV_DONE:
    case SR_EV_ABORT:
    case SR_EV_RPC:
        // TODO: rpc support for iproute2 commands.
    case SR_EV_UPDATE:
        return SR_ERR_OK;
    default:
        fprintf(stderr, "%s: unexpected sysrepo event: %u\n", __func__, sr_ev);
        return SR_ERR_INTERNAL;
    }

    return ret;
}

int apply_ipr2_cmd(char *ipr2_show_cmd)
{
    int ret;
    char **argv;
    int argc;

    parse_command(ipr2_show_cmd, &argc, &argv);
    jump_set = 1;
    if (setjmp(jbuf)) {
        // iproute2 exited, reset jump, and set exit callback.
        atexit(exit_cb);
        ret = SR_ERR_CALLBACK_FAILED;
        goto exit_check_done;
    }
    fprintf(stdout, "%s: executing command: %s\n", __func__, ipr2_show_cmd);
    ret = do_cmd(argc, argv);
exit_check_done:
    if (ret != EXIT_SUCCESS) {
        fprintf(stderr, "%s: iproute2 command execution failed, cmd = ", __func__);
        print_cmd_line(argc, argv);
        ret = SR_ERR_CALLBACK_FAILED;
    } else {
        ret = SR_ERR_OK;
    }
    jump_set = 0;
    free_argv2(argv, argc);
    return ret;
}

int ipr2_oper_get_items_cb(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name,
                           const char *xpath, const char *request_xpath, uint32_t request_id,
                           struct lyd_node **parent, void *private_data)
{
    return load_module_data(session, module_name, LYS_CONFIG_R, parent);
}

int load_linux_running_config()
{
    int ret = 0;
    struct lyd_node *root_node = NULL;
    fprintf(stdout, "%s: Started loading iproute2 running configuration.\n", __func__);
    for (size_t i = 0; i < sizeof(ipr2_ip_modules) / sizeof(ipr2_ip_modules[0]); i++) {
        fprintf(stdout, "%s: Loading module: %s data.\n", __func__, ipr2_ip_modules[i].module);
        load_module_data(sr_session, ipr2_ip_modules[i].module, LYS_CONFIG_W, &root_node);
    }

    fprintf(stdout, "%s: Storing loaded data to sysrepo running datastore.\n", __func__);
    ret = sr_edit_batch(sr_session, root_node, "replace");
    if (SR_ERR_OK != ret) {
        fprintf(stderr, "%s: Error by sr_edit_batch: %s.\n", __func__, sr_strerror(ret));
        goto cleanup;
    }

    // Commit changes
    ret = sr_apply_changes(sr_session, 0);
    if (SR_ERR_OK != ret) {
        fprintf(stderr, "%s: Error by sr_apply_changes: %s.\n", __func__, sr_strerror(ret));
        goto cleanup;
    }
    fprintf(stdout, "%s: Done loading iproute2 running configuration successfully.\n", __func__);
cleanup:
    lyd_free_all(root_node);
    root_node = NULL;
    return ret;
}

static void sr_subscribe_config()
{
    int ret;
    fprintf(stdout, "%s: Subscribing to iproute2 modules config changes:\n", __func__);

    /* subscribe to ip modules */
    for (size_t i = 0; i < sizeof(ipr2_ip_modules) / sizeof(ipr2_ip_modules[0]); i++) {
        ret = sr_module_change_subscribe(sr_session, ipr2_ip_modules[i].module, NULL,
                                         ip_sr_config_change_cb, NULL, 0, SR_SUBSCR_DEFAULT,
                                         &sr_sub_ctx);
        if (ret != SR_ERR_OK)
            fprintf(stderr, "%s: Failed to subscribe to module (%s) config changes: %s\n", __func__,
                    ipr2_ip_modules[i].module, sr_strerror(ret));
        else
            fprintf(stdout, "%s: Successfully subscribed to module (%s) config changes\n", __func__,
                    ipr2_ip_modules[i].module);
    }

    /* TODO subscribe to bridge modules */
    /* TODO subscribe to TC modules */
}

static void sr_subscribe_operational_pull()
{
    int ret;
    fprintf(stdout, "%s: Subscribing to iproute2 modules operational data pull requests:\n",
            __func__);
    /* subscribe to ip modules */
    for (size_t i = 0; i < sizeof(ipr2_ip_modules) / sizeof(ipr2_ip_modules[0]); i++) {
        ret = sr_oper_get_subscribe(sr_session, ipr2_ip_modules[i].module,
                                    ipr2_ip_modules[i].oper_pull_path, ipr2_oper_get_items_cb, NULL,
                                    0, &sr_sub_ctx);
        if (ret != SR_ERR_OK)
            fprintf(stderr,
                    "%s: Failed to subscribe to module (%s) operational data pull requests: %s\n",
                    __func__, ipr2_ip_modules[i].module, sr_strerror(ret));
        else
            fprintf(stdout,
                    "%s: Successfully subscribed to module (%s) operational data pull requests\n",
                    __func__, ipr2_ip_modules[i].module);
    }

    /* TODO subscribe to bridge modules */
    /* TODO subscribe to TC modules */
}

int sysrepo_start()
{
    int ret;

    ++json; /* set iproute2 to format its print outputs in json */
    ++show_details; /* set iproute2 to include details in its print outputs */
    ++show_stats; /* set iproute2 to include stats in its print outputs */

    ret = sr_connect(SR_CONN_DEFAULT, &sr_connection);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_connect(): %s\n", __func__, sr_strerror(ret));
        goto cleanup;
    }

    /* Start session. */
    ret = sr_session_start(sr_connection, SR_DS_RUNNING, &sr_session);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_session_start(): %s\n", __func__, sr_strerror(ret));
        goto cleanup;
    }
    load_linux_running_config();
    sr_subscribe_config();
    sr_subscribe_operational_pull();

    /* loop until ctrl-c is pressed / SIGINT is received */
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    while (!exit_application) {
        sleep(1);
    }

cleanup:
    if (sr_sub_ctx)
        sr_unsubscribe(sr_sub_ctx);
    if (sr_session)
        sr_session_stop(sr_session);
    if (sr_connection)
        sr_disconnect(sr_connection);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int ret;

    if (rtnl_open(&rth, 0) < 0)
        return EXIT_FAILURE;

    if (argc == 1) {
        atexit(exit_cb);
        return sysrepo_start();
    } else
        ret = do_cmd(argc - 1, argv + 1);

    rtnl_close(&rth);
    return ret;
}
