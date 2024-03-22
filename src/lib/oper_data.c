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
#include <libyang/tree_data.h>

#include "json-c/json.h"
#include "oper_data.h"
#include "cmdgen.h"

/* to be merged with cmdgen */
typedef enum {
    // leaf extensions
    OPER_ARG_NAME_EXT,
    OPER_VALUE_MAP_EXT,
    OPER_FLAG_MAP_EXT,
    OPER_CK_ARGNAME_PRESENCE_EXT
} oper_extension_t;

/* to be merged with cmdgen */
char *oper_yang_ext_map[] = { [OPER_ARG_NAME_EXT] = "oper-arg-name",
                              [OPER_VALUE_MAP_EXT] = "oper-value-map",
                              [OPER_FLAG_MAP_EXT] = "oper-flag-map",
                              [OPER_CK_ARGNAME_PRESENCE_EXT] = "oper-ck-argname-presence" };

/**
 * Struct store yang modules names and their iproute2 show cmd start.
 */
struct module_sh_cmdstart {
    const char *module_name; // Stores Module name
    const char *showcmd_start; // Store iproute2 show command related to the module
} ipr2_sh_cmdstart[] = { { "iproute2-ip-link", "ip link show" },
                         { "iproute2-ip-nexthop", "ip nexthop show" },
                         { "iproute2-ip-netns", "ip netns show" } };

void process_node(const struct lysc_node *s_node, json_object *json_array_obj, uint16_t lys_flags,
                  struct lyd_node **parent_data_node);

/**
 * Retrieves the iproute2 show command for a specific module name.
 * @param [in] module_name: Name of the module for which the shell command is to be retrieved.
 * @return show command line string, NULL if no matching command is found or on failure..
 */
char *get_module_sh_startcmd(const char *module_name)
{
    /* convert module name to show cmd */
    for (size_t i = 0; i < sizeof(ipr2_sh_cmdstart) / sizeof(ipr2_sh_cmdstart[0]); i++) {
        if (strcmp(module_name, ipr2_sh_cmdstart[i].module_name) == 0) {
            return strdup(ipr2_sh_cmdstart[i].showcmd_start);
        }
    }
    return NULL;
}

void free_list_params(const char ***key_values, int **values_lengths, int key_count)
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
 * @param [out] found_obj: JSON object to store the found key object.
 * @return json_bool, returns 1 if key is found, 0 if not.
 */
JSON_EXPORT json_bool find_json_value_by_key(struct json_object *jobj, const char *key,
                                             struct json_object **found_obj)
{
    enum json_type jtype = json_object_get_type(jobj);

    if (json_object_get_type(jobj) == json_type_object) {
        struct json_object_iterator it = json_object_iter_begin(jobj);
        struct json_object_iterator itEnd = json_object_iter_end(jobj);

        while (!json_object_iter_equal(&it, &itEnd)) {
            const char *current_key = json_object_iter_peek_name(&it);
            struct json_object *current_val = json_object_iter_peek_value(&it);

            if (strcmp(current_key, key) == 0) {
                *found_obj = current_val;
                return 1; /* key found */
            }

            if (find_json_value_by_key(current_val, key, found_obj))
                return 1; /* key found */

            json_object_iter_next(&it);
        }
    } else if (jtype == json_type_array) {
        for (int i = 0; i < json_object_array_length(jobj); i++) {
            struct json_object *arr_item = json_object_array_get_idx(jobj, i);
            if (find_json_value_by_key(arr_item, key, found_obj))
                return 1;
        }
    }
    return 0; // Key not found
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
    int ret = EXIT_SUCCESS;
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
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }

                // Store the new key
                (*key_values)[key_count] = strdup(json_object_get_string(temp_value));
                if (!(*key_values)[key_count]) {
                    fprintf(stderr, "Failed to duplicate list key value\n");
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
                (*values_lengths)[key_count] = strlen(json_object_get_string(temp_value));
                key_count++;
            } else {
                // key value not found in json data.
                ret = EXIT_FAILURE;
                goto cleanup;
            }
        }
    }
    (*keys_num) = key_count;
    ret = EXIT_SUCCESS;
cleanup:
    free(key_name);
    return ret;
}

/**
 * Converts a semicolon-separated key-value string to a JSON object. Each pair is separated by ';', and key-value within a pair is separated by ':'.
 * This function is useful for converting OPER_VALUE_MAP_EXT and OPER_FLAG_MAP_EXT mapping from string to JSON objects for easier manipulation and access.
 * @param [in] input: A semicolon-separated key-value pairs string.
 * @return A pointer to a JSON object representing the input string's key-value pairs. The caller is responsible for freeing the JSON object.
 */
json_object *strmap_to_jsonmap(const char *input)
{
    struct json_object *jobj = json_object_new_object();
    char *pairs = strdup(input);
    char *pair;
    char *rest_pairs = pairs;

    while ((pair = strtok_r(rest_pairs, ";", &rest_pairs))) {
        char *rest_kv = pair;
        char *key = strtok_r(rest_kv, ":", &rest_kv);
        char *value = strtok_r(NULL, ":", &rest_kv);

        if (key && value) {
            json_object_object_add(jobj, key, json_object_new_string(value));
        }
    }

    free(pairs);
    return jobj;
}

/**
 * Looks up the value associated with a given key in a JSON object and returns it. If the key isn't found, returns the original value.
 * 
 * For a given value (original_value) this function helps mapping it to its corresponding mapped value found in value map JSON Object.
 * If orignal_value doesn't have a corresponding mapping, it gets returned as is.
 * @param [in] value_map_jobj: JSON object containing key-value mappings.
 * @param [in] original_value: The key to look up in the mapping.
 * @return The mapped value if the key is found; otherwise, the original value is returned.
 */
const char *map_value_if_needed(struct json_object *value_map_jobj, const char *original_value)
{
    struct json_object *mapped_value_obj;
    if (value_map_jobj &&
        json_object_object_get_ex(value_map_jobj, original_value, &mapped_value_obj)) {
        return json_object_get_string(mapped_value_obj);
    }
    return original_value;
}

/**
 * This function compairs schema node (s_node) parents names to lyd_node data tree node name, if the 
 * schema node parent containers are not added to the lyd_node, this function adds them.
 * 
 * This function is to be used before processing leafs and leaf_lists to ensure their parent continers
 * are added to the lyd data tree.
 * 
 * @param [in] s_node: schema node being processed (typically the s_node of a leaf or leaf_list).
 * @param [in] parent_data_node: lyd data tree to which nodes under process are being attached to.
 */
void add_missing_parents(const struct lysc_node *s_node, struct lyd_node **parent_data_node)
{
    struct lyd_node *new_data_node = NULL;
    if (strcmp(s_node->parent->name, (*parent_data_node)->schema->name) != 0 &&
        s_node->parent->nodetype == LYS_CONTAINER) {
        // check grand_parents
        add_missing_parents(s_node->parent, parent_data_node);
        if (LY_SUCCESS ==
            lyd_new_inner(*parent_data_node, NULL, s_node->parent->name, 0, &new_data_node)) {
            *parent_data_node = new_data_node;
        }
    }
}

/**
 * Processes a JSON array of flags into individual leaf nodes under a parent data node, applying flag mappings if provided.
 * 
 * This function converts JSON flag arrays into YANG data tree leafs, with optional value mapping via a flag map JSON object.
 * @param [in] temp_obj: JSON array object containing flags to be processed.
 * @param [in] fmap_jobj: JSON object containing flag-value mappings.
 * @param [in,out] parent_data_node: Pointer to the parent data node to attach the created leaf nodes.
 * @param [in] s_node: Schema node corresponding to the leaf nodes to be created.
 */
void flags_to_leafs(struct json_object *temp_obj, struct json_object *fmap_jobj,
                    struct lyd_node **parent_data_node, const struct lysc_node *s_node)
{
    const char *value = NULL;
    json_object_object_foreach(fmap_jobj, key_flag, flag_map)
    {
        size_t n_json_flags = json_object_array_length(temp_obj);
        for (size_t i = 0; i < n_json_flags; i++) {
            struct json_object *json_flag_obj = json_object_array_get_idx(temp_obj, i);
            const char *json_flag = json_object_get_string(json_flag_obj);
            if (strcmp(key_flag, json_flag) == 0) {
                value = json_object_get_string(flag_map);
                break;
            }
        }
        break; // break after the first iteration, we need to check only the first element of fmap json object.
    }
    if (!value) {
        json_object *flag_unset_obj = NULL;
        json_object_object_get_ex(fmap_jobj, "FLAG-UNSET", &flag_unset_obj);
        value = json_object_get_string(flag_unset_obj);
    }
    if (LY_SUCCESS != lyd_new_term(*parent_data_node, NULL, s_node->name, value, 0, NULL)) {
        fprintf(stderr, "node %s creation failed\n", s_node->name);
    }
}

/**
 * Creates yang leaf node using JSON object keys and values, if the leaf arg_name is found 
 * in JSON Object keys, this function will create a new leaf attached to parent data tree 
 * and set its value to the JSON Object key value.
 * This function will also handle json value to leaf value type mapping.
 * 
 * @param [in] json_obj: JSON object containing the data.
 * @param [in] arg_name: Argument name (leaf name) to look up in the JSON object keys.
 * @param [in,out] parent_data_node: Pointer to the parent data node to attach the created leaf node.
 * @param [in] s_node: Schema node corresponding to the leaf node to be created.
 */
void jdata_to_leaf(struct json_object *json_obj, const char *arg_name,
                   struct lyd_node **parent_data_node, const struct lysc_node *s_node)
{
    char *vmap_str = NULL, *fmap_str = NULL, *static_value;
    if (get_lys_extension(OPER_FLAG_MAP_EXT, s_node, &fmap_str) == EXIT_SUCCESS) {
        if (fmap_str == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-flag-map extension found but failed to "
                    "get the flag_map value for node \"%s\"\n",
                    __func__, s_node->name);
            return;
        }
    } else if (get_lys_extension(OPER_VALUE_MAP_EXT, s_node, &vmap_str) == EXIT_SUCCESS) {
        if (vmap_str == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-value-map extension found but failed to "
                    "get the value_map value for node \"%s\"\n",
                    __func__, s_node->name);
            return;
        }
    }

    struct json_object *fmap_jobj = fmap_str ? strmap_to_jsonmap(fmap_str) : NULL;
    if (fmap_str) {
        free(fmap_str);
    }

    struct json_object *vmap_jobj = vmap_str ? strmap_to_jsonmap(vmap_str) : NULL;
    if (vmap_str) {
        free(vmap_str);
    }

    struct json_object *temp_obj = NULL;
    // Attempt to directly find the argument name or use search function
    json_bool argname_found = json_object_object_get_ex(json_obj, arg_name, &temp_obj);
    if (!argname_found) {
        argname_found = find_json_value_by_key(json_obj, arg_name, &temp_obj);
    }

    if (argname_found) { // Proceed only if arg_name is found as a key
        if (get_lys_extension(OPER_CK_ARGNAME_PRESENCE_EXT, s_node, &static_value) ==
            EXIT_SUCCESS) {
            if (static_value == NULL) {
                fprintf(stderr,
                        "%s: ipr2cgen:oper-ck-argname-presence extension found but failed to "
                        "get the static_value for node \"%s\"\n",
                        __func__, s_node->name);
                return;
            }
            add_missing_parents(s_node, parent_data_node);
            if (LY_SUCCESS !=
                lyd_new_term(*parent_data_node, NULL, s_node->name, static_value, 0, NULL)) {
                fprintf(stderr, "node %s creation failed\n", s_node->name);
            }
            free(static_value);
        } else if (temp_obj) {
            add_missing_parents(s_node, parent_data_node);
            if (json_object_is_type(temp_obj, json_type_array) && fmap_jobj) {
                // array values are processed as flags.
                flags_to_leafs(temp_obj, fmap_jobj, parent_data_node, s_node);
            } else {
                // Process a single value
                const char *value =
                    map_value_if_needed(vmap_jobj, json_object_get_string(temp_obj));
                if (LY_SUCCESS !=
                    lyd_new_term(*parent_data_node, NULL, s_node->name, value, 0, NULL)) {
                    fprintf(stderr, "node %s creation failed\n", s_node->name);
                }
            }
        }
    }
cleanup:
    if (vmap_jobj) {
        json_object_put(vmap_jobj);
    }
    if (fmap_jobj) {
        json_object_put(fmap_jobj);
    }
}

/**
 * Creates yang leaf-list node using JSON object array values, if the leaf-list name is found in 
 * JSON Object keys, This function will create a new leaf-list attached to parent data tree and 
 * set its leaf-list values to the JSON Object key values set.
 * 
 * This function will also handle value mapping and flag mapping before setting the leaf value.
 * @param [in] json_array_obj: JSON object containing the data to search in.
 * @param [in] arg_name: Argument name (leaf-list name) to look up in the JSON object keys.
 * @param [in,out] parent_data_node: Pointer to the parent data node to attach the created leaf-list node.
 * @param [in] s_node: Schema node corresponding to the leaf-list node to be created.
 */
void jdata_to_leaflist(struct json_object *json_array_obj, const char *arg_name,
                       struct lyd_node **parent_data_node, const struct lysc_node *s_node)
{
    char *vmap_str = NULL;
    if (get_lys_extension(OPER_VALUE_MAP_EXT, s_node, &vmap_str) == EXIT_SUCCESS) {
        if (vmap_str == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-value-map extension found but failed to "
                    "get the value_map value for node \"%s\"\n",
                    __func__, s_node->name);
            return;
        }
    }

    struct json_object *vmap_jobj = vmap_str ? strmap_to_jsonmap(vmap_str) : NULL;
    if (vmap_str) {
        free(vmap_str);
    }

    struct json_object *temp_obj = NULL;
    // Attempt to directly find the argument name or use search function
    if (!json_object_object_get_ex(json_array_obj, arg_name, &temp_obj)) {
        find_json_value_by_key(json_array_obj, arg_name, &temp_obj);
    }

    // Proceed only if temp_obj is found
    if (temp_obj) {
        add_missing_parents(s_node, parent_data_node);
        // leaf-list expects a json array outputs
        if (json_object_is_type(temp_obj, json_type_array)) {
            size_t n_json_flags = json_object_array_length(temp_obj);
            for (size_t i = 0; i < n_json_flags; i++) {
                struct json_object *inner_value = json_object_array_get_idx(temp_obj, i);
                const char *value =
                    map_value_if_needed(vmap_jobj, json_object_get_string(inner_value));
                if (LY_SUCCESS !=
                    lyd_new_term(*parent_data_node, NULL, s_node->name, value, 0, NULL)) {
                    fprintf(stderr, "%s: node %s creation failed.\n", s_node->name, __func__);
                }
            }
        }
    }

    if (vmap_jobj) {
        json_object_put(vmap_jobj);
    }
}

void single_jobj_to_list(struct json_object *json_obj, struct lyd_node **parent_data_node,
                         const struct lysc_node *s_node, uint16_t lys_flags)
{
    const struct lysc_node_list *list = (const struct lysc_node_list *)s_node;
    const char **key_values;
    int *values_lengths, keys_count;
    if (get_list_keys(list, json_obj, &key_values, &values_lengths, &keys_count) == EXIT_SUCCESS) {
        add_missing_parents(s_node, parent_data_node);
        struct lyd_node *new_data_node = NULL;
        lyd_new_list3(*parent_data_node, NULL, s_node->name, key_values, values_lengths, 0,
                      &new_data_node);
        free_list_params(&key_values, &values_lengths, keys_count);

        if (!new_data_node)
            new_data_node = *parent_data_node;

        // Recursively process child nodes
        const struct lysc_node *s_child;
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            process_node(s_child, json_obj, lys_flags, &new_data_node);
        }
    }
}

/**
 * Converts JSON data into a YANG list(s) and attaches it to YANG parant data node.
 * This function parses a JSON object, identified by a key, into a list according to 
 * a specified YANG schema node, If the key is not found, it attempts to treat the 
 * entire JSON object as a single list item.
 *
 * @param [in] json_obj: Pointer to the JSON object containing data to be converted.
 * @param [in] arg_name: Key name to identify the target data within the JSON object.
 * @param [in] s_node: Pointer to the schema node defining the list's structure in the YANG model.
 * @param [in] lys_flags: Flags for processing options based on YANG model requirements.
 * @param [in] parent_data_node: the parent node in the data tree where the new list nodes will be added.
 */
void jdata_to_list(struct json_object *json_obj, const char *arg_name,
                   const struct lysc_node *s_node, uint16_t lys_flags,
                   struct lyd_node **parent_data_node)
{
    struct json_object *lists_arrays;
    if (find_json_value_by_key(json_obj, arg_name, &lists_arrays)) {
        if (json_object_get_type(lists_arrays) == json_type_array) {
            size_t n_arrays = json_object_array_length(lists_arrays);
            for (size_t i = 0; i < n_arrays; i++) {
                struct json_object *list_obj = json_object_array_get_idx(lists_arrays, i);
                single_jobj_to_list(list_obj, parent_data_node, s_node, lys_flags);
            }
        } else {
            single_jobj_to_list(lists_arrays, parent_data_node, s_node, lys_flags);
        }
    } else {
        // Handle case where json_array_obj itself is the list to process
        single_jobj_to_list(json_obj, parent_data_node, s_node, lys_flags);
    }
}

/**
 * Processes a schema node and maps its data from a JSON object to a YANG data tree (lyd_node).
 * This function looks up schema node names in json object keys to retrive their values, and creates
 * corresponding lyd_node.
 * @param [in] s_node: Schema node to be processed.
 * @param [in] json_obj: JSON object containing the data to map.
 * @param [in] lys_flags: flag value used to filter out schema nodes containers and leafs.
 *                        Use LYS_CONFIG_R to allow parsing read-only containers and leafs (for loading operational data).
 *                        Use LYS_CONFIG_W to allow parsing write containers and leafs (for loading configuration data).
 * @param [in, out] parent_data_node: Pointer to the parent data node in the YANG data tree (lyd_node).
 */
void process_node(const struct lysc_node *s_node, json_object *json_obj, uint16_t lys_flags,
                  struct lyd_node **parent_data_node)
{
    /* Create lyd_node for current schema node and attach to parent_data_node if not NULL */
    if (*parent_data_node == NULL) { // Top-level node
        lyd_new_inner(NULL, s_node->module, s_node->name, 0, parent_data_node);
    }

    const struct lysc_node *s_child;
    struct lyd_node *new_data_node = *parent_data_node;

    /* check for schema extension overrides */
    char *arg_name = NULL;
    if (get_lys_extension(OPER_ARG_NAME_EXT, s_node, &arg_name) == EXIT_SUCCESS) {
        if (arg_name == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-arg-name extension found but failed to "
                    "get the arg-name value for node \"%s\"\n",
                    __func__, s_node->name);
            return;
        }
    } else {
        arg_name = strdup(s_node->name);
    }

    switch (s_node->nodetype) {
    case LYS_LEAFLIST:
    case LYS_LEAF: {
        /* json keys are added on list creation, don't re-add them */
        if (lysc_is_key(s_node))
            break;

        /* on config load don't process read-only leafs
               on operational load, don't process write leafs */
        if ((lys_flags & LYS_CONFIG_W) && (s_node->flags & LYS_CONFIG_R))
            break;
        if ((lys_flags & LYS_CONFIG_R) && (s_node->flags & LYS_CONFIG_W))
            break;

        if (s_node->nodetype == LYS_LEAFLIST)
            jdata_to_leaflist(json_obj, arg_name, parent_data_node, s_node);

        if (s_node->nodetype == LYS_LEAF)
            jdata_to_leaf(json_obj, arg_name, parent_data_node, s_node);

        free(arg_name);
        break;
    }
    case LYS_LIST: {
        jdata_to_list(json_obj, arg_name, s_node, lys_flags, parent_data_node);
        free(arg_name);
        break;
    }
    case LYS_CHOICE:
    case LYS_CASE:
    case LYS_CONTAINER:
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            process_node(s_child, json_obj, lys_flags, &new_data_node);
        }
        break;
    default:
        break;
    }
}

/**
 * Converts JSON array objects to a YANG data tree based on the provided schema node.
 * This function is a wrapper around `process_node` for handling JSON arrays.
 * @param [in] json_array_obj: JSON array object to convert.
 * @param [in] top_node: Top-level schema node for mapping.
 * @param [in] lys_flags: Flag value used to filter out schema nodes containers and leafs
 *                        Use LYS_CONFIG_R to allow parsing read-only containers and leafs (for loading operational data).
 *                        Use LYS_CONFIG_W to allow parsing write containers and leafs (for loading configuration data).
 * @param [out] data_tree: Pointer to store the resulting YANG data tree.
 */
void jarray_to_lyd(json_object *json_array_obj, const struct lysc_node *top_node,
                   uint16_t lys_flags, struct lyd_node **data_tree)
{
    const struct lysc_node *node;
    LY_LIST_FOR(top_node, node)
    {
        // Start with level 0 for top-level nodes, data_tree = NULL
        process_node(node, json_array_obj, lys_flags, data_tree);
    }
}

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
int sr_set_oper_data_items(sr_session_ctx_t *session, const char *module_name, uint16_t lys_flags,
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

    if (json_object_get_type(json_obj) == json_type_array) {
        // Iterate over json_obj arrays:
        size_t n_arrays = json_object_array_length(json_obj);
        for (size_t i = 0; i < n_arrays; i++) {
            struct json_object *array_obj = json_object_array_get_idx(json_obj, i);
            jarray_to_lyd(array_obj, module->compiled->data, lys_flags, &data_tree);

            /* 
            Merge data_tree in parent node.
            This operation will validate data_tree nodes before merging them.
            It will drop nodes with validation failed
            */
            if (lyd_merge_tree(parent, data_tree, LYD_MERGE_DEFAULTS)) {
                /* Merge of one json_array data_tree failed.
               This is a partial failure, no need to return ERR. */
                fprintf(stderr, "Partial failure on pushing '%s' operational data\n",
                        data_tree->schema->name, __func__);
            }
            // Free data_tree after merging it.
            lyd_free_tree(data_tree);
            data_tree = NULL;
        }
    }

cleanup:
    json_object_put(json_obj);
    sr_release_context(sr_session_get_connection(session));
    return ret;
}
