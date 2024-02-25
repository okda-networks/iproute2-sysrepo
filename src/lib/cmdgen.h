/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_CMDGEN_H
#define IPROUTE2_SYSREPO_CMDGEN_H

#include <libyang/libyang.h>
#include <sysrepo.h>
#include <bsd/string.h>

#define CMDS_ARRAY_SIZE 1024
#define CMD_LINE_SIZE 1024

/**
 * extension to extension name map.
 */
extern char *yang_ext_map[];

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
 * definition of yang's extension_t.
 */
typedef enum {
    // list extensions
    CMD_START_EXT,
    // root container extensions
    CMD_ADD_EXT,
    CMD_DELETE_EXT,
    CMD_UPDATE_EXT,

    // leaf extensions
    ARG_NAME_EXT,
    FLAG_EXT,
    VALUE_ONLY_EXT,
    VALUE_ONLY_ON_UPDATE_EXT,

    //other
    ON_UPDATE_INCLUDE

} extension_t;

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
 * get the yang operation from the lyd_node (create, replace, delete)
 * @param [in]dnode lyd_node
 * @return oper_t the time of the operation found on this lyd_node.
 */
oper_t get_operation(const struct lyd_node *dnode);

/**
 * get the extension from lyd_node,
 * @param [in] ex_t extension_t to be captured from lyd_node.
 * @param [in] dnode lyd_node where to search for provided ext.
 * @param [out] value extension value if found. can be null.
 * @return EXIT_SUCCESS if ext found, EXIT_FAILURE if not found
 */
int get_extension(extension_t ex_t, const struct lyd_node *dnode, char **value);

#endif// IPROUTE2_SYSREPO_CMDGEN_H