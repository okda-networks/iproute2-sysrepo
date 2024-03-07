/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_OPER_DATA_H
#define IPROUTE2_SYSREPO_OPER_DATA_H

#include <sysrepo.h>
#include <sysrepo/xpath.h>

extern char json_buffer[1024 * 1024]; /* holds iproute2 show commands json outputs */

char *get_module_sh_startcmd(const char *module_name);
int sr_set_oper_data_items(sr_session_ctx_t *session, const char *module_name,
                           struct lyd_node **parent);

#endif // IPROUTE2_SYSREPO_OPER_DATA_H
