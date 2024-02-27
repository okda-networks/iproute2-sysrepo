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

int lyd_leaf2arg(struct lyd_node *element, bool only_lyd_list_keys, char **arg_name, char **arg_value) {
    if (only_lyd_list_keys && !lysc_is_key(element->schema)){
        *arg_name = NULL;
        *arg_value = NULL;
        return EXIT_SUCCESS;
    }

    /* set arg_name */
    if (get_extension(VALUE_ONLY_EXT, element, NULL) == EXIT_SUCCESS) {
            *arg_name = NULL;
    } else {
        *arg_name = strdup(element->schema->name);
    }
    
    /* set arg_value */
    if (element->schema->nodetype == LYS_LEAF){
        LY_DATA_TYPE type = ((struct lysc_node_leaf *)element->schema)->type->basetype;
        if (type == LY_TYPE_IDENT) {
            *arg_value = strdup(get_identityref_value(lyd_get_value(element)));
        } else {
            *arg_value = strdup(lyd_get_value(element));
        }
    }
    
    //printf("a-arg_name %s\na-arg_value %s\n", *arg_name, *arg_value);
    
    return EXIT_SUCCESS;
}

int lyd_leaf2rollback_arg(struct lyd_node *element, bool only_lyd_list_keys, char **arg_name, char **arg_value) {
    
    if (only_lyd_list_keys && !lysc_is_key(element->schema)){
        *arg_name = NULL;
        *arg_value = NULL;
        return EXIT_SUCCESS;
    }

    /* set arg_name */
    if (get_extension(VALUE_ONLY_EXT, element, NULL) == EXIT_SUCCESS) {
            *arg_name = NULL;
    } else {
        *arg_name = strdup(element->schema->name);
    }
    
    /* set arg_value */

    if (element->schema->nodetype == LYS_LEAF || element->schema->nodetype == LYS_LEAFLIST){
        
        struct lyd_meta *meta_prev_value, *meta_default_value;
        /* "orig-value" metadata contains the previous value */
        meta_prev_value = lyd_find_meta(element->meta, NULL, "yang:orig-value");
        /* "orig-default" holds the previous default flag value */
        meta_default_value = lyd_find_meta(element->meta, NULL, "yang:orig-default");



        if (meta_prev_value)
            printf("node name %s, meta %s\n",element->schema->name,lyd_get_meta_value(meta_prev_value));

        
        const char *prev_value, *default_value, *rollback_value;
        prev_value = lyd_get_meta_value(meta_prev_value);
        default_value = lyd_get_meta_value(meta_default_value);
        rollback_value = NULL;
        // if node value have been changed, it would have prev_value set, otherwise node didn't get change
        if (prev_value)
            rollback_value = prev_value;
        else {
            rollback_value = lyd_get_value(element);
        }
        LY_DATA_TYPE type = ((struct lysc_node_leaf *)element->schema)->type->basetype;
        if (type == LY_TYPE_IDENT) {
            *arg_value = strdup(get_identityref_value(rollback_value));
        } else {
            *arg_value = strdup(rollback_value);
        }
    }
    return EXIT_SUCCESS;
}


struct lyd_node *sr_get_xpath_lyd(char * xpath){
    sr_data_t *subtree = NULL;
    int error = 0;
	error = sr_get_subtree(sr_session, xpath, 0, &subtree);

    return subtree->tree;
}

struct lyd_node *sr_get_lyd_node_by_name(char *node_name, struct lyd_node *parent_node) {
    int ret;
    
    char xpath[512] = {0};
    
    lyd_path(parent_node, LYD_PATH_STD, xpath, 512);
    strlcat(xpath, "/", sizeof(xpath));
    strlcat(xpath, node_name, sizeof(xpath));
    
    sr_data_t *node_data = NULL;
    
    // Fetch the node from sysrepo
    if (sr_get_node(sr_session, xpath, 0, &node_data) != SR_ERR_OK) {
        fprintf(stderr, "%s: failed to get include node data from sysrepo. Node name = \"%s\"\n", __func__, node_name);
        return NULL;
    }
    return node_data->tree;
}

struct cmd_info* lyd2rollback_cmd(const struct lyd_node *startcmd_node) {
    const struct lyd_node *root_node = &(startcmd_node->parent->node);
    char cmd_line[CMD_LINE_SIZE] = {0};
    char *oper2cmd_prefix[3] = {NULL};

    // first get the add, update, delete cmds prefixis from schema extensions
    if (get_prefix_cmds(root_node, oper2cmd_prefix) == NULL) {
        return NULL;
    }

    // get the lyd_node change operation
    oper_t op_val = get_operation(startcmd_node);
    if (op_val == UNKNOWN_OPR) {
        fprintf(stderr, "%s: unknown operation for startcmd node (%s) \n",
                __func__, startcmd_node->schema->name);
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
    LYD_TREE_DFS_BEGIN(startcmd_node, element){
            char *arg_name = NULL, *arg_value = NULL;
            char *on_update_include = NULL;

            switch (op_val) {
            case DELETE_OPR:
                lyd_leaf2arg(element, true, &arg_name, &arg_value);
                // add keys only + include-on-delete
                break;
            case ADD_OPR:
                // fetch node from sysrepo (data before change)
                // add everything in fetched node
                break;
            case UPDATE_OPR:
                // add leafs arg_name and previous value (pay attention to flag)
                if (element->schema->nodetype == LYS_LEAF || element->schema->nodetype == LYS_LEAFLIST){
                    lyd_leaf2rollback_arg(element, false, &arg_name, &arg_value);
                }
                // add include-on-update leafs
                if (element->schema->nodetype == LYS_CONTAINER || element->schema->nodetype == LYS_LIST) {
                    if (get_extension(ON_UPDATE_INCLUDE, element, &on_update_include) == EXIT_SUCCESS){
                        if (on_update_include == NULL) {
                            fprintf(stderr, "%s: ON_UPDATE_INCLUDE extension found,"
                                            "but failed to retrieve the arg-name list from ON_UPDATE_INCLUDE extension for node \"%s\" \n",
                                    __func__, element->schema->name);
                            return NULL;
                        }
                    }
                    char *token;
                    token = strtok(on_update_include, ",");
                    
                    while (token != NULL) {
                        struct lyd_node *node = sr_get_lyd_node_by_name(token,element);
                        lyd_leaf2arg(node, false, &arg_name, &arg_value);
                        if (arg_name != NULL) {
                            strlcat(cmd_line, " ", sizeof(cmd_line));
                            strlcat(cmd_line, arg_name, sizeof(cmd_line));
                            arg_name = NULL;
                            }
                        if (arg_value != NULL) {
                            strlcat(cmd_line, " ", sizeof(cmd_line));
                            strlcat(cmd_line, arg_value, sizeof(cmd_line));
                            arg_value = NULL;
                        }
                    }
                    goto next_iter;
                }
                break;
            }

update_cmd:
            if (arg_name != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, arg_name, sizeof(cmd_line));
                }
            if (arg_value != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, arg_value, sizeof(cmd_line));
            }
next_iter:
        LYD_TREE_DFS_END(startcmd_node, element);
    }

    if (cmd_line)
        printf("result rollback cmd: %s\n",cmd_line);
}