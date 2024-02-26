/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Amjad Daraiseh, <adaraiseh@okdanetworks.com>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU Affero General Public
 *              License Version 3.0 as published by the Free Software Foundation;
 *              either version 3.0 of the License, or (at your option) any later
 *              version.
 *
 * Copyright (C) 2024 Okda Networks, <contact@okdanetworks.com>
 */

#include <sysrepo.h>

#include "cmdgen.h"

extern sr_session_ctx_t *sr_session;

const char* get_identityref_value(const char* identityref) {
    const char *colon = strchr(identityref, ':');
    if (colon != NULL) {
        return colon + 1;
    }
    return identityref;
}

char **get_prefix_cmds(const struct lyd_node *root_node, char **oper2cmd_prefix) {
    if (get_extension(CMD_ADD_EXT, root_node, &oper2cmd_prefix[ADD_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: cmd-add extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-add\n",
                __func__);
        return NULL;
    }
    if (get_extension(CMD_DELETE_EXT, root_node, &oper2cmd_prefix[DELETE_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: cmd-delete extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-delete\n",
                __func__);
        return NULL;
    }
    if (get_extension(CMD_UPDATE_EXT, root_node, &oper2cmd_prefix[UPDATE_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: ipr2cgen:cmd-update extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-update\n",
                __func__);
        return NULL;
    }

    return oper2cmd_prefix; // Returning the updated array on success
}

int lyd_leaf2arg(struct lyd_node *element, char **arg_name, char **arg_value) {
    LY_DATA_TYPE type;

    /* set arg_name */
    if (get_extension(FLAG_EXT, element, NULL) == EXIT_SUCCESS) {
        if (!strcmp("true", lyd_get_value(element))) {
            *arg_name = strdup(element->schema->name);
        }
    } else if (get_extension(VALUE_ONLY_EXT, element, NULL) == EXIT_SUCCESS) {
        *arg_name = NULL;
    }

    /* set arg_value */
    type = ((struct lysc_node_leaf *)element->schema)->type->basetype;
    if (type == LY_TYPE_IDENT) {
        *arg_value = strdup(get_identityref_value(lyd_get_value(element)));
    } else {
        *arg_value = strdup(lyd_get_value(element));
    }
    printf("a-arg_name %s\na-arg_value %s\n", *arg_name, *arg_value);
    return EXIT_SUCCESS;
}



struct lyd_node *sr_get_xpath_lyd(char * xpath){
    sr_data_t *subtree = NULL;
    int error = 0;
	error = sr_get_subtree(sr_session, xpath, 0, &subtree);

    return subtree->tree;
}

struct cmd_info* lyd2rollback_cmd(const struct lyd_node *change_node)
{
    const struct lyd_node *root_node = &(change_node->parent->node);
    char cmd_line[CMD_LINE_SIZE] = {0};
    char *oper2cmd_prefix[3] = {NULL};

    // first get the add, update, delete cmds prefixis from schema extensions
    if (get_prefix_cmds(root_node, oper2cmd_prefix) == NULL) {
        return NULL;
    }

    // get the lyd_node change operation
    oper_t op_val = get_operation(change_node);
    if (op_val == UNKNOWN_OPR) {
        fprintf(stderr, "%s: unknown operation for startcmd node (%s) \n",
                __func__, change_node->schema->name);
        return NULL;
    }

    // invert lyd_node add/delete operation (for rollback)
    if (op_val == ADD_OPR)
        op_val = DELETE_OPR;    
    else if (op_val == DELETE_OPR)
        op_val = ADD_OPR;

    // add prefix cmd to command line
    strlcpy(cmd_line, oper2cmd_prefix[op_val], sizeof(cmd_line));

    struct lyd_node *element;
    LYD_TREE_DFS_BEGIN(change_node, element){
        printf("node name %s\n", element->schema->name);
        if (element->schema->nodetype == LYS_LEAF) {
            char *arg_name = NULL, *arg_value = NULL;
            char *on_update_include = NULL;
            switch (op_val) {
                case DELETE_OPR:
                    // add keys only + include-on-delete
                    break;
                case ADD_OPR:
                    // fetch node from sysrepo (data before change)
                    // add everything in fetched node
                    break;
                case UPDATE_OPR:
                    // add include-on-update leafs + everything in lyd element (using previous values)
                    if (get_extension(ON_UPDATE_INCLUDE, element, &on_update_include) == EXIT_SUCCESS){
                        if (on_update_include == NULL) {
                            fprintf(stderr, "%s: ON_UPDATE_INCLUDE extension found,"
                                            "but failed to retrieve the arg-name list from ON_UPDATE_INCLUDE extension for node \"%s\" \n",
                                    __func__, element->schema->name);
                            return NULL;
                        }
                    }
                    // add leafs arg_name and arg_value (pay attention to flag)
                    lyd_leaf2arg(element, &arg_name, &arg_value);
                    break;
            }

            if (arg_name != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, arg_name, sizeof(cmd_line));
            }
            if (arg_value != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, arg_value, sizeof(cmd_line));
            }
        }
        LYD_TREE_DFS_END(change_node, element);
    }
    if (cmd_line)
        printf("result rollback cmd: %s\n",cmd_line);
}