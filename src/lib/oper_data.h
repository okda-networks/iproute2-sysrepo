/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_OPER_DATA_H
#define IPROUTE2_SYSREPO_OPER_DATA_H

#include <sysrepo.h>

extern char json_buffer[1024 * 1024]; /* holds iproute2 show commands json outputs */

/**
 * Sets operational data items or running data items for a module in a Sysrepo session
 * based on global json_buffer content.
 * This function parses the json_buffer data outputs comming from iproute2 show commands
 * then converts it to a YANG data tree, and merges the created data tree into a parent
 * data tree.
 * @param [in] session: Sysrepo session context.
 * @param [in] module_name: Name of the module for which the data is to be set.
 * @param [in] lys_flags: Flag value used to filter out schema nodes containers and leafs.
 *                        Use LYS_CONFIG_R to allow parsing read-only containers and leafs (for loading operational data).
 *                        Use LYS_CONFIG_W to allow parsing write containers and leafs (for loading configuration data).
 * @param [in, out] parent: Pointer to the parent node for merging the data tree.
 * @return Returns an integer status code (SR_ERR_OK on success or an error code on failure).
 */
int load_module_data(sr_session_ctx_t *session, const char *module_name, uint16_t lys_flags,
                     struct lyd_node **parent);

#endif // IPROUTE2_SYSREPO_OPER_DATA_H
