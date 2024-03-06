/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Amjad Daraiseh, adaraiseh@okdanetworks.com>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU Affero General Public
 *              License Version 3.0 as published by the Free Software Foundation;
 *              either version 3.0 of the License, or (at your option) any later
 *              version.
 *
 * Copyright (C) 2024 Okda Networks, <contact@okdanetworks.com>
 */

#include "oper_data.h"
#include "cmdgen.h"

/**
 * Struct store yang modules names and their iproute2 show cmd start.
 */
struct module_sh_cmdstart {
    const char *module_name; // Stores Module name
    const char *showcmd_start; // Store iproute2 show command related to the module
} ipr2_sh_cmdstart[] = { { "iproute2-ip-link", "ip link show" },
                         { "iproute2-ip-nexthop", "ip nexthop show" },
                         { "iproute2-ip-netns", "ip netns show" } };

/**
 * Wrap a JSON array in a JSON object with a specified key.
 * 
 * @param json_array The JSON array to wrap.
 * @param key The key under which to wrap the JSON array.
 * @return A new string representing the wrapped JSON object. The caller is responsible for freeing this memory.
 */
char *wrap_json_array(const char *json_array, const char *key)
{
    // Calculate the length of the new JSON string
    size_t new_json_length = strlen(json_array) + strlen(key) +
                             4; // Extra 4 chars for the two braces and two quotation marks
    char *wrapped_json = malloc(new_json_length + 1); // +1 for the null terminator

    if (!wrapped_json) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    // Construct the new JSON string
    sprintf(wrapped_json, "{\"%s\":%s}", key, json_array);

    return wrapped_json;
}

char *get_module_sh_startcmd(const char *module_name)
{
    char showcmd[CMD_LINE_SIZE] = { 0 };

    /* convert module name to show cmd */
    for (size_t i = 0; i < sizeof(ipr2_sh_cmdstart) / sizeof(ipr2_sh_cmdstart[0]); i++) {
        if (strcmp(module_name, ipr2_sh_cmdstart[i].module_name) == 0) {
            strlcat(showcmd, ipr2_sh_cmdstart[i].showcmd_start, sizeof(showcmd));
            break;
        }
    }

    if (showcmd == NULL) {
        return NULL;
    }
    fprintf(stdout, "show cmd: %s\n", showcmd);

    struct cmd_info *cmd = malloc(sizeof(struct cmd_info));
    if (cmd == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    return strdup(showcmd);
}

int sr_set_oper_data_items(sr_session_ctx_t *session, const char *module_name,
                           struct lyd_node **parent)
{
    const struct ly_ctx *ly_ctx;

    char *schema = NULL;
    const struct lys_module *module;
    ly_ctx = sr_acquire_context(sr_session_get_connection(session));

    /* get the requested module yang schema */
    module = ly_ctx_get_module_implemented(ly_ctx, module_name);

    /* TODO update the module leafs from json_buffer */

cleanup:
    sr_release_context(sr_session_get_connection(session));
    return SR_ERR_OK;
}
