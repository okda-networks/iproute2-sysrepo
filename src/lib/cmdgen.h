/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_CMDGEN_H
#define IPROUTE2_SYSREPO_CMDGEN_H

#include <libyang/libyang.h>

#define CMDS_ARRAY_SIZE 1024

/**
 * @brief data struct to store cmd operation.
 *
 */
typedef enum {
    ADD_OPR,
    DELETE_OPR,
    UPDATE_OPR,
    UNKNOWN_OPR,
} oper_t;

/**
 * @brief data struct to store the int argc,char **argv pair.
 *
 * this struct can be easily used by
 * iproute2::do_cmd(argc,argv)
 */
struct cmd_args{
    int argc;
    char **argv;
    const struct lyd_node *cmd_start_dnode;
};

/**
 * @brief generate argc,argv commands out of the lyd_node (diff).
 *
 *
 * @param[in] node Data node diff to generate the argc, argv.
 * @return Array of cmd_args struct.
 */
struct cmd_args** lyd2cmds_argv(const struct lyd_node *change_node);

/**
 * @brief generate argc,argv rollback command out of the lyd_node (diff).
 *
 *
 * @param[in] node Data node diff to generate the argc, argv.
 * @return Array of cmd_args struct.
 */
struct cmd_args* lyd2rollback_cmds(struct cmd_args **ipr2_cmds);
oper_t get_operation(const struct lyd_node *dnode);

#endif// IPROUTE2_SYSREPO_CMDGEN_H