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
 * @brief data struct to store command information.
 */
struct cmd_info{
    int argc;
    char **argv;
    const struct lyd_node *cmd_start_dnode; /** lyd change node where the command starts
                                                ipr2gen:cmd-start) */
};

/**
 * @brief generate list of commands info from the lyd_node (diff).
 *
 *
 * @param[in] change_node lyd_node of change data.
 * @return cmd_info array.
 */
struct cmd_info** lyd2cmds(const struct lyd_node *change_node);

/**
 * @brief convert change cmd info to rollback cmd info.
 *
 *
 * @param[in] change_node lyd_node of change data.
 * @return cmd_info rollback cmd.
 */
struct cmd_info* lyd2rollback_cmd(const struct lyd_node *change_node);

/**
 * @brief get the change operation from change lyd_node.
 *
 *
 * @param[in] lyd_node change node.
 * @return oper_t
 */
oper_t get_operation(const struct lyd_node *dnode);

#endif// IPROUTE2_SYSREPO_CMDGEN_H