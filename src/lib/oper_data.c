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

#include <stdbool.h>
#include "json-c/json.h"

#include "oper_data.h"
#include "cmdgen.h"

/* to be merged with cmdgen */
typedef enum {
    // leaf extensions
    OPER_ARG_NAME_EXT
} oper_extension_t;

/* to be merged with cmdgen */
char *oper_yang_ext_map[] = { [OPER_ARG_NAME_EXT] = "oper-arg-name" };

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
 * Retrieves the iproute2 show command for a specific module name.
 * @param [in] module_name: Name of the module for which the shell command is to be retrieved.
 * @return show command line string, NULL if no matching command is found or on failure..
 */
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
    fprintf(stdout, "iproute2 show cmd: %s\n", showcmd);

    return strdup(showcmd);
}

void free_key_vals_lens(const char ***key_values, int **values_lengths, int key_count)
{
    if (*key_values) {
        for (int i = 0; i < key_count; ++i) {
            free((void *)(*key_values)[i]);
        }
        free(*key_values);
        *key_values = NULL;
    }

    if (*values_lengths) {
        free(*values_lengths);
        *values_lengths = NULL;
    }
}

/**
 * Recursively searches for a value associated with a given key within a JSON object.
 * This function can be used to search in nested JSON objects and arrays.
 * @param [in] jobj: JSON object to be searched.
 * @param [in] key: Key string for which the value is to be found.
 * @return Returns the JSON object associated with the key if found; otherwise, returns NULL.
 */
struct json_object *find_json_value_by_key(struct json_object *jobj, const char *key)
{
    if (!jobj || !key)
        return NULL;

    enum json_type type = json_object_get_type(jobj);
    struct json_object *found_val = NULL;

    if (type == json_type_object) {
        struct json_object_iterator it = json_object_iter_begin(jobj);
        struct json_object_iterator itEnd = json_object_iter_end(jobj);

        while (!json_object_iter_equal(&it, &itEnd)) {
            const char *current_key = json_object_iter_peek_name(&it);
            struct json_object *current_val = json_object_iter_peek_value(&it);

            if (strcmp(current_key, key) == 0) {
                return current_val;
            }

            found_val = find_json_value_by_key(current_val, key);
            if (found_val)
                return found_val;

            json_object_iter_next(&it);
        }
    } else if (type == json_type_array) {
        for (int i = 0; i < json_object_array_length(jobj); i++) {
            struct json_object *arr_item = json_object_array_get_idx(jobj, i);
            found_val = find_json_value_by_key(arr_item, key);
            if (found_val)
                return found_val;
        }
    }
    return NULL; // Key not found
}

// TODO : redundant code to cmdgen:get_extension, input is lysc_node instead of lyd_node
int get_lys_extension(oper_extension_t ex_t, const struct lysc_node *s_node, char **value)
{
    struct lysc_ext_instance *ys_extenstions = s_node->exts;
    if (ys_extenstions == NULL)
        return EXIT_FAILURE;

    LY_ARRAY_COUNT_TYPE i;
    LY_ARRAY_FOR(ys_extenstions, i)
    {
        if (!strcmp(ys_extenstions[i].def->name, oper_yang_ext_map[ex_t])) {
            if (value != NULL)
                *value = strdup(ys_extenstions[i].argument);
            return EXIT_SUCCESS;
        }
    }
    return EXIT_FAILURE;
}

/**
 * Extracts keys and their values from a JSON array object based on the YANG list Schema keys.
 * This function is used to map JSON array data to YANG schema list keys.
 * @param [in] list: YANG list schema node.
 * @param [in] json_array_obj: JSON object containing the list's data.
 * @param [out] key_values: Pointer to store the extracted keys' values.
 * @param [out] values_lengths: Pointer to store the lengths of the keys' values.
 * @param [out] keys_num: Pointer to store the number of keys extracted.
 * @return Returns EXIT_SUCCESS if keys are successfully extracted; otherwise, returns EXIT_FAILURE.
 */
int get_list_keys(const struct lysc_node_list *list, json_object *json_array_obj,
                  const char ***key_values, int **values_lengths, int *keys_num)
{
    int key_count = 0;
    *key_values = NULL;
    *values_lengths = NULL;
    struct json_object *temp_value;
    char *key_name;

    const struct lysc_node *child;
    for (child = list->child; child; child = child->next) {
        if (lysc_is_key(child)) {
            if (get_lys_extension(OPER_ARG_NAME_EXT, child, &key_name) == EXIT_SUCCESS) {
                if (key_name == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-arg-name extension found but failed to "
                            "get the arg-name value for node \"%s\"\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }
            } else {
                key_name = strdup(child->name);
            }

            if (json_object_object_get_ex(json_array_obj, key_name, &temp_value)) {
                // Resize the arrays to accommodate key additions
                *key_values = (const char **)realloc(*key_values, (key_count + 1) * sizeof(char *));
                *values_lengths =
                    (uint32_t *)realloc(*values_lengths, (key_count + 1) * sizeof(uint32_t));

                if (!*key_values || !*values_lengths) {
                    fprintf(stderr, "Failed to allocate memory\n");
                    return EXIT_FAILURE;
                }

                // Store the new key
                (*key_values)[key_count] = strdup(json_object_get_string(temp_value));
                if (!(*key_values)[key_count]) {
                    fprintf(stderr, "Failed to duplicate list key value\n");
                    return EXIT_FAILURE;
                }
                (*values_lengths)[key_count] = strlen(json_object_get_string(temp_value));
                key_count++;
            } else {
                // key value not found in json data.
                return EXIT_FAILURE;
            }
        }
    }
    (*keys_num) = key_count;
    return EXIT_SUCCESS;
}

/**
 * Processes a schema node and maps its data from a JSON object to a YANG data tree (lyd_node).
 * This function looks up schema node names in json object keys to retrive their values, and creates
 * corresponding lyd_node.
 * This function only processes read-only schema nodes.
 * @param [in] s_node: Schema node to be processed.
 * @param [in] json_array_obj: JSON object containing the data to map.
 * @param [in, out] parent_data_node: Pointer to the parent data node in the YANG data tree (lyd_node).
 */
void process_node(const struct lysc_node *s_node, json_object *json_array_obj,
                  struct lyd_node **parent_data_node)
{
    struct lyd_node *new_data_node = NULL;
    struct json_object *temp_obj;

    // Create lyd_node for current schema node and attach to parent_data_node if not NULL
    if (*parent_data_node == NULL) { // Top-level node
        lyd_new_inner(NULL, s_node->module, s_node->name, 0, parent_data_node);
        new_data_node = *parent_data_node;
    } else {
        const struct lysc_node_list *list;
        switch (s_node->nodetype) {
        case LYS_LEAF:
        case LYS_LEAFLIST: {
            /* json keys are added on list creation, don't re-add them */
            if (lysc_is_key(s_node))
                break;

            /* don't process config write schema nodes */
            if (s_node->flags & LYS_CONFIG_W)
                break;

            /* check for schema extension overrides */
            char *arg_name = NULL;
            if (get_lys_extension(OPER_ARG_NAME_EXT, s_node, &arg_name) == EXIT_SUCCESS) {
                if (arg_name == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-arg-name extension found but failed to "
                            "get the arg-name value for node \"%s\"\n",
                            __func__, s_node->name);
                    break;
                }
            } else {
                arg_name = strdup(s_node->name);
            }

            /*
            To add json values to the lyd_leaf do the following: 
            1- if arg_name value is found on main array element keys:
                - if the found value is an array then add array values to leaf list.
                - else (the found value is one element), add the element to leaf.
            2- else (arg_name) value is not found on main array element keys:
                - search for key name: if found do same steps in 1. */
            if (json_object_object_get_ex(json_array_obj, arg_name, &temp_obj)) {
                if (json_object_is_type(temp_obj, json_type_array)) {
                    size_t n_arrays = json_object_array_length(temp_obj);
                    for (size_t i = 0; i < n_arrays; i++) {
                        struct json_object *inner_value = json_object_array_get_idx(temp_obj, i);
                        lyd_new_term(*parent_data_node, NULL, s_node->name,
                                     json_object_get_string(inner_value), 0, &new_data_node);
                    }
                } else {
                    lyd_new_term(*parent_data_node, NULL, s_node->name,
                                 json_object_get_string(temp_obj), 0, &new_data_node);
                }
            } else {
                temp_obj = find_json_value_by_key(json_array_obj, arg_name);
                if (temp_obj) {
                    if (json_object_is_type(temp_obj, json_type_array)) {
                        size_t n_arrays = json_object_array_length(temp_obj);
                        for (size_t i = 0; i < n_arrays; i++) {
                            struct json_object *inner_value =
                                json_object_array_get_idx(temp_obj, i);
                            lyd_new_term(*parent_data_node, NULL, s_node->name,
                                         json_object_get_string(inner_value), 0, &new_data_node);
                        }
                    } else {
                        lyd_new_term(*parent_data_node, NULL, s_node->name,
                                     json_object_get_string(temp_obj), 0, &new_data_node);
                    }
                }
            }
            break;
        }
        case LYS_CONTAINER:
            lyd_new_inner(*parent_data_node, NULL, s_node->name, 0, &new_data_node);
            break;
        case LYS_LIST: {
            const char **key_values;
            int *values_lengths, keys_count;
            list = (const struct lysc_node_list *)s_node;
            if (get_list_keys(list, json_array_obj, &key_values, &values_lengths, &keys_count) ==
                EXIT_SUCCESS) {
                lyd_new_list3(*parent_data_node, NULL, s_node->name, key_values, values_lengths, 0,
                              &new_data_node);
                free_key_vals_lens(&key_values, &values_lengths, keys_count);
            }
            break;
        case LYS_CHOICE:
        case LYS_CASE:
            break;
        }
        default:
            break;
        }
    }

    /* - Choice and case don't create new_data_node.
       - Some module lists may not be created if their keys are not found in json array.
       - For such cases: set parent for next iteration to be previous iteration parent */
    if (new_data_node == NULL)
        new_data_node = (*parent_data_node);

    // Recursively process child nodes, if any, passing current node as the next parent.
    const struct lysc_node *s_child;
    LY_LIST_FOR(lysc_node_child(s_node), s_child)
    {
        process_node(s_child, json_array_obj, &new_data_node);
    }
}

/**
 * Converts JSON array objects to a YANG data tree based on the provided schema node.
 * This function is a wrapper around `process_node` for handling JSON arrays.
 * @param [in] json_array_obj: JSON array object to convert.
 * @param [in] top_node: Top-level schema node for mapping.
 * @param [out] data_tree: Pointer to store the resulting YANG data tree.
 */
void jarray2lyd(json_object *json_array_obj, const struct lysc_node *top_node,
                struct lyd_node **data_tree)
{
    const struct lysc_node *node;
    LY_LIST_FOR(top_node, node)
    {
        // Start with level 0 for top-level nodes, data_tree = NULL
        process_node(node, json_array_obj, data_tree);
    }
}

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
                           struct lyd_node **parent)
{
    int ret = SR_ERR_OK;
    const struct ly_ctx *ly_ctx;
    const struct lys_module *module = NULL;
    struct lyd_node *data_tree = NULL;
    struct json_object *json_obj;

    ly_ctx = sr_acquire_context(sr_session_get_connection(session));
    json_obj = json_tokener_parse(json_buffer);
    module = ly_ctx_get_module_implemented(ly_ctx, module_name);
    if (module == NULL) {
        fprintf(stderr, "Failed to get requested module schema, module name: %s\n", module_name);
        ret = SR_ERR_CALLBACK_FAILED;
        goto cleanup;
    }
    // Iterate over json_obj arrays:
    size_t n_arrays = json_object_array_length(json_obj);
    for (size_t i = 0; i < n_arrays; i++) {
        struct json_object *array_obj = json_object_array_get_idx(json_obj, i);
        jarray2lyd(array_obj, module->compiled->data, &data_tree);

        /* Merge data_tree in parent node.
           This operation will validate data_tree nodes before merging them.
           It will drop nodes with validation failed */
        if (lyd_merge_tree(parent, data_tree, LYD_MERGE_DEFAULTS)) {
            lyd_free_tree(data_tree);

            /* Merge of one json_array data_tree failed.
               This is a partial failure, no need to return ERR. */
            goto cleanup;
        }
        // Free data_tree after merging it.
        lyd_free_tree(data_tree);
        data_tree = NULL;
    }

cleanup:
    json_object_put(json_obj);
    sr_release_context(sr_session_get_connection(session));
    return ret;
}
