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
#include "uapi/linux/rtnetlink.h"
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
#include "namespace.h"
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
int vlan_rtm_cur_ifidx = -1;
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
int linux_monitor_suspended = 0;

static void usage(void)
{
    fprintf(
        stderr,
        "Usage: iproute2-sysrepo [ --no-monitor ]\n"
        "   --no-monitor: run iproute2-sysrepo without monitoring and syncing linux config changes to sysrepo,\n"
        "                 PS: the linux config will be loaded to sysrepo at startup if if \"--no-monitor\" option enabled.\n"
        "                 by default the monitoring enabled.\"\n");
    exit(-1);
}

/**
 * Struct tp store yang modules names and their operational data subscription path.
 */
struct yang_module {
    const char *module; // Stores Module name
    const char *oper_pull_path; // Stores operational pull subscription path
} ipr2_ip_modules[] = { { "iproute2-ip-link", "/iproute2-ip-link:links" },
                        { "iproute2-ip-nexthop", "/iproute2-ip-nexthop:nexthops" },
                        { "iproute2-ip-netns", "/iproute2-ip-netns:netnses" },
                        { "iproute2-ip-route", "/iproute2-ip-route:routes" },
                        { "iproute2-ip-rule", "/iproute2-ip-rule:rules" },
                        { "iproute2-ip-neighbor", "/iproute2-ip-neighbor:neighbors" },
                        { "iproute2-tc-qdisc", "/iproute2-tc-qdisc:qdiscs" },
                        { "iproute2-tc-filter", "/iproute2-tc-filter:tc-filters" } };

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

int netns_switch2(char *name)
{
    char net_path[PATH_MAX];
    int netns;
    unsigned long mountflags = 0;

    snprintf(net_path, sizeof(net_path), "%s/%s", NETNS_RUN_DIR, name);
    netns = open(net_path, O_RDONLY | O_CLOEXEC);
    if (netns < 0) {
        fprintf(stderr, "Cannot open network namespace \"%s\": %s\n", name, strerror(errno));
        return -1;
    }

    if (setns(netns, CLONE_NEWNET) < 0) {
        fprintf(stderr, "setting the network namespace \"%s\" failed: %s\n", name, strerror(errno));
        close(netns);
        return -1;
    }
    close(netns);
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
    const char *libbpf_version;
    char *batch_file = NULL;
    char *basename;
    int ret = 0;

    int netns_fd = 0;
    // switch to default network namespace, as prev commands might changed the netns.
    int fd = open("/proc/1/ns/net", O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    // Switch to the default network namespace
    if (setns(fd, CLONE_NEWNET) == -1) {
        perror("setns");
        close(fd);
        exit(EXIT_FAILURE);
    }
    // Close the file descriptor
    close(fd);

    /* to run vrf exec without root, capabilities might be set, drop them
	 * if not needed as the first thing.
	 * execv will drop them for the child command.
	 * vrf exec requires:
	 * - cap_dac_override to create the cgroup subdir in /sys
	 * - cap_bpf to load the BPF program
	 * - cap_net_admin to set the socket into the cgroup
	 */
    if (argc < 3 || strcmp(argv[1], "vrf") != 0 || strcmp(argv[2], "exec") != 0)
        drop_cap();

    basename = strrchr(argv[0], '/');
    if (basename == NULL)
        basename = argv[0];
    else
        basename++;
    max_flush_loops = 10;
    const struct cmd *cmds;
    const struct cmd *c;

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
    while (argc > 1) {
        char *opt = argv[1];

        if (strcmp(opt, "--") == 0) {
            argc--;
            argv++;
            break;
        }
        if (opt[0] != '-')
            break;
        if (opt[1] == '-')
            opt++;
        if (matches(opt, "-loops") == 0) {
            argc--;
            argv++;
            if (argc <= 1)
                missarg("loop count");
            max_flush_loops = atoi(argv[1]);
        } else if (matches(opt, "-family") == 0) {
            argc--;
            argv++;
            if (argc <= 1)
                missarg("family type");
            else
                preferred_family = read_family(argv[1]);
            if (preferred_family == AF_UNSPEC)
                invarg("invalid protocol family", argv[1]);
        } else if (strcmp(opt, "-4") == 0) {
            preferred_family = AF_INET;
        } else if (strcmp(opt, "-6") == 0) {
            preferred_family = AF_INET6;
        } else if (strcmp(opt, "-0") == 0) {
            preferred_family = AF_PACKET;
        } else if (strcmp(opt, "-M") == 0) {
            preferred_family = AF_MPLS;
        } else if (strcmp(opt, "-B") == 0) {
            preferred_family = AF_BRIDGE;
        } else if (matches(opt, "-human") == 0 || matches(opt, "-human-readable") == 0) {
            ++human_readable;
        } else if (matches(opt, "-iec") == 0) {
            ++use_iec;
        } else if (matches(opt, "-stats") == 0 || matches(opt, "-statistics") == 0) {
            ++show_stats;
        } else if (matches(opt, "-details") == 0) {
            ++show_details;
        } else if (matches(opt, "-resolve") == 0) {
            ++resolve_hosts;
        } else if (matches(opt, "-oneline") == 0) {
            ++oneline;
        } else if (matches(opt, "-timestamp") == 0) {
            ++timestamp;
        } else if (matches(opt, "-tshort") == 0) {
            ++timestamp;
            ++timestamp_short;
        } else if (matches(opt, "-force") == 0) {
            ++force;
        } else if (matches(opt, "-brief") == 0) {
            ++brief;
        } else if (matches(opt, "-json") == 0) {
            ++json;
        } else if (matches(opt, "-netns") == 0) {
            NEXT_ARG();
            if (netns_switch2(argv[1]))
                exit(-1);
            netns_fd = netns_get_fd(argv[1]);
        } else if (matches(opt, "-Numeric") == 0) {
            ++numeric;
        } else if (matches(opt, "-all") == 0) {
            do_all = true;
        } else if (strcmp(opt, "-echo") == 0) {
            ++echo_request;
        } else {
            fprintf(stderr, "Option \"%s\" is unknown, try \"ip -help\".\n", opt);
            exit(-1);
        }
        argc--;
        argv++;
    }
    char *argv0 = argv[1];
    int arg_skip = 2;
    if ((strlen(basename) > 2) && !strcmp(argv[0], "ip")) {
        argv0 = basename + 2;
        arg_skip = 3;
    }
    // we need to rtnl_open() for every command in order to have netns_switch() to work.
    if (rtnl_open(&rth, 0) < 0)
        return EXIT_FAILURE;

    for (c = cmds; c->cmd; ++c) {
        if (matches(argv0, c->cmd) == 0) {
            ret = -(c->func(argc - arg_skip, argv + arg_skip));
            rtnl_close(&rth);
            if (netns_fd) {
                close(netns_fd);
            }
            return ret;
        }
    }
    fprintf(stderr,
            "Unknown argument \"%s\".\n"
            "\nPossible execution options:\n"
            "1- Run with no argumentss to start iproute2-sysrepo.\n"
            "2- Run with individual iproute2 commands arguments.\n",
            argv[1]);

    rtnl_close(&rth);
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
    // if the change is comming from our session then ignore the change, might be the moniotr.
    if (!strcmp(sr_session_get_orig_name(session), "ipr2-sr"))
        return SR_ERR_OK;

    sr_change_iter_t *it;
    int ret;
    struct lyd_node *root_dnode;
    const struct lyd_node *dnode;
    linux_monitor_suspended = 1;
    sr_change_oper_t sr_op;
    sr_acquire_context(sr_connection);
    ret = sr_get_changes_iter(session, "//*", &it);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_get_changes_iter() failed for \"%s\"", __func__, module_name);
        goto done;
    }

    // we don't iterate through all changes, we just get the first changed node, then find it's root.
    // this root will have all the changes. NOTE: this approach will not work if the yang has more than
    // root containers. which is not the case in iproute2-yang modules.
    ret = sr_get_change_tree_next(session, it, &sr_op, &dnode, NULL, NULL, NULL);
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: failed to get next change node: %s\n", __func__, sr_strerror(ret));

        ret = SR_ERR_INTERNAL;
        goto done;
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
        ret = SR_ERR_OK;
        break;
    default:
        fprintf(stderr, "%s: unexpected sysrepo event: %u\n", __func__, sr_ev);
        ret = SR_ERR_INTERNAL;
    }
done:
    sr_release_context(sr_connection);
    linux_monitor_suspended = 0;
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
    return load_module_data(session, module_name, LYS_CONFIG_R | LYS_CONFIG_W, parent, "1");
}

struct load_linux_runcfg_arg {
    const char *module_name;
    struct lyd_node **root_node;
};

int load_linux_runcfg_ns_cb(char *nsname, void *arg)
{
    struct load_linux_runcfg_arg *runcfg_arg = (struct load_linux_runcfg_arg *)arg;
    load_module_data(sr_session, runcfg_arg->module_name, LYS_CONFIG_W, runcfg_arg->root_node,
                     nsname);
    return 0;
}

int load_linux_config_for_all_netns(const char *module_name, struct lyd_node **root_node)
{
    fprintf(stdout, "%s: Loading module: %s data for all NETNS.\n", __func__, module_name);
    // load data from default netns
    load_module_data(sr_session, module_name, LYS_CONFIG_W, root_node, "1");
    // load data from all netns
    struct load_linux_runcfg_arg runcfg_arg = { module_name, root_node };
    netns_foreach(load_linux_runcfg_ns_cb, &runcfg_arg);
    return EXIT_SUCCESS;
}

int load_linux_running_config()
{
    int ret;
    struct lyd_node *root_node = NULL;
    sr_acquire_context(sr_connection);

    fprintf(stdout, "%s: Started loading iproute2 running configuration.\n", __func__);
    for (size_t i = 0; i < sizeof(ipr2_ip_modules) / sizeof(ipr2_ip_modules[0]); i++) {
        load_linux_config_for_all_netns(ipr2_ip_modules[i].module, &root_node);
    }

    fprintf(stdout, "%s: Storing loaded data to sysrepo running datastore.\n", __func__);
    ret = sr_edit_batch(sr_session, root_node, "replace");
    if (SR_ERR_OK != ret) {
        fprintf(stderr, "%s: Error by sr_edit_batch: %s.\n", __func__, sr_strerror(ret));
        goto cleanup;
    }
    ret = sr_apply_changes(sr_session, 0);
    if (SR_ERR_OK != ret) {
        fprintf(stderr, "%s: Error by sr_apply_changes: %s.\n", __func__, sr_strerror(ret));
        goto cleanup;
    }
    fprintf(stdout, "%s: Done loading iproute2 running configuration successfully.\n", __func__);
cleanup:
    lyd_free_all(root_node);
    sr_discard_changes(sr_session);
    sr_release_context(sr_connection);

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
}

static int accept_msg2(struct rtnl_ctrl_data *ctrl, struct nlmsghdr *n, void *arg)
{
    int ret;
    struct lyd_node *root_node = NULL;

    sr_session_ctx_t *sr_session2;
    sr_acquire_context(sr_connection);

    char *module_name;
    switch (n->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_DELLINK:
        module_name = "iproute2-ip-link";
        break;

    case RTM_NEWNEIGH:
    case RTM_DELNEIGH:
        module_name = "iproute2-ip-neighbor";
        break;

    case RTM_NEWMDB:
    case RTM_DELMDB:
        module_name = "iproute2-ip-link";
        break;

    case RTM_NEWVLAN:
    case RTM_DELVLAN:
        module_name = "iproute2-ip-link";
        break;
    case RTM_NEWROUTE:
    case RTM_DELROUTE:
        module_name = "iproute2-ip-route";
        break;

    case RTM_NEWRULE:
    case RTM_DELRULE:
        module_name = "iproute2-ip-rule";
        break;

    case RTM_NEWQDISC:
    case RTM_DELQDISC:
        module_name = "iproute2-tc-qdisc";
        break;

    case RTM_NEWTUNNEL:
    case RTM_DELTUNNEL:
        module_name = "iproute2-ip-link";
        break;
    default:
        module_name = "iproute2-ip-link";
    }
    if (linux_monitor_suspended)
        return EXIT_SUCCESS;

    fprintf(
        stdout,
        "%s: new change detected on linux config for module = %s, loading config to sysrepo...\n",
        __func__, module_name);

    load_linux_config_for_all_netns(module_name, &root_node);
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
cleanup:
    lyd_free_all(root_node);
    sr_discard_changes(sr_session);
    root_node = NULL;
    sr_release_context(sr_connection);

    return 0;
}

void *do_monitor2_thd(void *args)
{
    char *netns_name = args; // Avoid unused parameter warning
    unsigned int groups = ~RTMGRP_TC;
    struct rtnl_handle rth_mon = { .fd = -2 };
    preferred_family = AF_UNSPEC;

    if (netns_name == NULL) {
        // switch back to default netns first.,
        int fd = open("/proc/1/ns/net", O_RDONLY);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        // Switch to the default network namespace
        if (setns(fd, CLONE_NEWNET) == -1) {
            perror("setns");
            close(fd);
            exit(EXIT_FAILURE);
        }
        // Close the file descriptor
        close(fd);
    } else {
        netns_switch2(netns_name);
    }

    rtnl_close(&rth_mon);
    if (rtnl_open(&rth_mon, groups) < 0)
        exit(1);

    ll_init_map(&rth_mon);
    if (rtnl_listen(&rth_mon, accept_msg2, args) < 0)
        exit(2);
    return 0;
}

int load_linux_confg_monitor_ns_cb(char *nsname, void *arg)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, do_monitor2_thd, nsname) != 0) {
        fprintf(stderr, "Error creating thread\n");
    }

    return 0;
}

void start_linux_config_monitor_thds()
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, do_monitor2_thd, NULL) != 0) {
        fprintf(stderr, "Error creating thread\n");
    }
    // start monitoring thread for the rest of netns.
    netns_foreach(load_linux_confg_monitor_ns_cb, NULL);
}

int sysrepo_start(int do_monitor)
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
    sr_session_set_orig_name(sr_session, "ipr2-sr");
    if (ret != SR_ERR_OK) {
        fprintf(stderr, "%s: sr_session_start(): %s\n", __func__, sr_strerror(ret));
        goto cleanup;
    }

    load_linux_running_config();
    sr_subscribe_config();
    sr_subscribe_operational_pull();
    if (do_monitor)
        start_linux_config_monitor_thds();

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
    int monitor = 1;
    if (argc <= 2) {
        if (argc == 2) {
            if (!strcmp(argv[1], "--no-monitor")) {
                monitor = 0;
            } else if (!strcmp(argv[1], "help")) {
                usage();
            } else {
                fprintf(stderr, "Unknown argument \"%s\"\n", argv[1]);
                return EXIT_FAILURE;
            }
        }
        atexit(exit_cb);
        return sysrepo_start(monitor);
    } else
        ret = do_cmd(argc - 1, argv + 1);

    return ret;
}
