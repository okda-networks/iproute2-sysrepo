/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_OPER_DATA_H
#define IPROUTE2_SYSREPO_OPER_DATA_H

#include <sysrepo.h>
#include <sysrepo/xpath.h>

extern char json_buffer[1024 * 1024]; /* holds iproute2 show commands json outputs */

/**
 * Retrieves the iproute2 show command for a specific module name.
 * @param [in] module_name: Name of the module for which the shell command is to be retrieved.
 * @return show command line string, NULL if no matching command is found or on failure..
 */
char *get_module_sh_startcmd(const char *module_name);

/**
 * Sets operational data items for a module in a Sysrepo session based on global json_buffer content.
 * This function parses the json_buffer data, converts it to a YANG data tree, and merges it into the 
 * existing operational datastore.
 * @param [in] session: Sysrepo session context.
 * @param [in] module_name: Name of the module for which the data is to be set.
 * @param [in, out] parent: Pointer to the parent node for merging the data tree.
 * @return Returns an integer status code (SR_ERR_OK on success or an error code on failure).
 */
int sr_set_oper_data_items(sr_session_ctx_t *session, const char *module_name,
                           struct lyd_node **parent);

#endif // IPROUTE2_SYSREPO_OPER_DATA_H
