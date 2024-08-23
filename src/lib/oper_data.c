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

char *net_namespace;

/* to be merged with cmdgen */
typedef enum {
    // leaf extensions
    OPER_CMD_EXT,
    OPER_INNER_CMD_EXT,
    OPER_ARG_NAME_EXT,
    OPER_VALUE_MAP_EXT,
    OPER_FLAG_MAP_EXT,
    OPER_CK_ARGNAME_PRESENCE_EXT,
    OPER_DEFAULT_VALUE_EXT,
    OPER_STOP_IF_EXT,
    OPER_COMBINE_VALUES_EXT,
    OPER_SUB_JOBJ_EXT,
    OPER_DUMP_TC_FILTERS,
} oper_extension_t;

/* to be merged with cmdgen */
char *oper_yang_ext_map[] = { [OPER_CMD_EXT] = "oper-cmd",
                              [OPER_INNER_CMD_EXT] = "oper-inner-cmd",
                              [OPER_ARG_NAME_EXT] = "oper-arg-name",
                              [OPER_VALUE_MAP_EXT] = "oper-value-map",
                              [OPER_FLAG_MAP_EXT] = "oper-flag-map",
                              [OPER_CK_ARGNAME_PRESENCE_EXT] = "oper-ck-argname-presence",
                              [OPER_DEFAULT_VALUE_EXT] = "oper-default-val",
                              [OPER_STOP_IF_EXT] = "oper-stop-if",
                              [OPER_COMBINE_VALUES_EXT] = "oper-combine-values",
                              [OPER_SUB_JOBJ_EXT] = "oper-sub-jobj",
                              [OPER_DUMP_TC_FILTERS] = "oper-dump-tc-filters" };

extern int apply_ipr2_cmd(char *ipr2_show_cmd);
int process_node(const struct lysc_node *s_node, json_object *json_array_obj, uint16_t lys_flags,
                 struct lyd_node **parent_data_node);

void generate_tc_sh_cmds(char **commands, int *command_count, char *tc_filter_type,
                         const char *dev_name, const char *qdisc_kind, const char *ingress_block,
                         const char *egress_block)
{
    if (*command_count >= CMDS_ARRAY_SIZE) {
        fprintf(stderr, "%s: Command buffer overflow.\n", __func__);
        return;
    }

    int result;

    if (strcmp(tc_filter_type, "shared-block-filter") == 0) {
        if (strcmp(qdisc_kind, "ingress") == 0 && ingress_block) {
            result = snprintf(commands[*command_count], CMD_LINE_SIZE, "tc filter show block %s",
                              ingress_block);
            if (result < 0 || result >= CMD_LINE_SIZE) {
                fprintf(stderr, "%s: snprintf failed or truncated for ingress block.\n", __func__);
                return;
            }
            (*command_count)++;
        } else if (strcmp(qdisc_kind, "clsact") == 0) {
            if (ingress_block) {
                result = snprintf(commands[*command_count], CMD_LINE_SIZE,
                                  "tc filter show block %s", ingress_block);
                if (result < 0 || result >= CMD_LINE_SIZE) {
                    fprintf(stderr,
                            "%s: snprintf failed or truncated for ingress block (clsact).\n",
                            __func__);
                    return;
                }
                (*command_count)++;
            }
            if (egress_block) {
                result = snprintf(commands[*command_count], CMD_LINE_SIZE,
                                  "tc filter show block %s", egress_block);
                if (result < 0 || result >= CMD_LINE_SIZE) {
                    fprintf(stderr, "%s: snprintf failed or truncated for egress block.\n",
                            __func__);
                    return;
                }
                (*command_count)++;
            }
        }
    } else if (strcmp(tc_filter_type, "dev-filter") == 0 && (!ingress_block || !egress_block)) {
        if (strcmp(qdisc_kind, "ingress") == 0) {
            result = snprintf(commands[*command_count], CMD_LINE_SIZE,
                              "tc filter show dev %s ingress", dev_name);
            if (result < 0 || result >= CMD_LINE_SIZE) {
                fprintf(stderr, "%s: snprintf failed or truncated for dev ingress filter.\n",
                        __func__);
                return;
            }
            (*command_count)++;
        } else if (strcmp(qdisc_kind, "clsact") == 0) {
            result = snprintf(commands[*command_count], CMD_LINE_SIZE,
                              "tc filter show dev %s ingress", dev_name);
            if (result < 0 || result >= CMD_LINE_SIZE) {
                fprintf(stderr,
                        "%s: snprintf failed or truncated for dev ingress filter (clsact).\n",
                        __func__);
                return;
            }
            (*command_count)++;

            result = snprintf(commands[*command_count], CMD_LINE_SIZE,
                              "tc filter show dev %s egress", dev_name);
            if (result < 0 || result >= CMD_LINE_SIZE) {
                fprintf(stderr, "%s: snprintf failed or truncated for dev egress filter.\n",
                        __func__);
                return;
            }
            (*command_count)++;
        }
    } else if (strcmp(tc_filter_type, "qdisc-filter") == 0) {
        result =
            snprintf(commands[*command_count], CMD_LINE_SIZE, "tc filter show dev %s", dev_name);
        if (result < 0 || result >= CMD_LINE_SIZE) {
            fprintf(stderr, "%s: snprintf failed or truncated for qdisc filter.\n", __func__);
            return;
        }
        (*command_count)++;
    }
}

/* generates tc filter commands using the information in interfaces qdiscs */
void qdiscs_to_filters_cmds(struct json_object *qdisc_array, char *tc_filter_type, char **commands,
                            int *command_count)
{
    size_t n_qdiscs = json_object_array_length(qdisc_array);

    for (size_t i = 0; i < n_qdiscs; i++) {
        struct json_object *qdisc = json_object_array_get_idx(qdisc_array, i);
        if (!qdisc) {
            fprintf(stderr, "%s: Failed to retrieve qdisc object at index %zu.\n", __func__, i);
            continue;
        }

        struct json_object *kind, *dev, *ingress_block, *egress_block;

        if (!json_object_object_get_ex(qdisc, "kind", &kind) ||
            !json_object_object_get_ex(qdisc, "dev", &dev)) {
            fprintf(stderr, "%s: Missing required qdisc information at index %zu.\n", __func__, i);
            continue;
        }

        const char *kind_str = json_object_get_string(kind);
        const char *dev_str = json_object_get_string(dev);
        if (kind_str == NULL || dev_str == NULL) {
            fprintf(stderr, "%s: Error converting JSON object to string at index %zu.\n", __func__,
                    i);
            continue;
        }

        const char *ingress_block_str = NULL;
        const char *egress_block_str = NULL;

        if (json_object_object_get_ex(qdisc, "ingress_block", &ingress_block)) {
            ingress_block_str = json_object_get_string(ingress_block);
            if (ingress_block_str == NULL) {
                fprintf(stderr,
                        "%s: Error converting ingress block JSON object to string at index %zu.\n",
                        __func__, i);
                continue;
            }
        }
        if (json_object_object_get_ex(qdisc, "egress_block", &egress_block)) {
            egress_block_str = json_object_get_string(egress_block);
            if (egress_block_str == NULL) {
                fprintf(stderr,
                        "%s: Error converting egress block JSON object to string at index %zu.\n",
                        __func__, i);
                continue;
            }
        }

        if (*command_count >= CMDS_ARRAY_SIZE) {
            fprintf(stderr, "%s: Command buffer overflow.\n", __func__);
            return;
        }

        generate_tc_sh_cmds(commands, command_count, tc_filter_type, dev_str, kind_str,
                            ingress_block_str, egress_block_str);
    }
}

void free_list_params(const char ***key_values, uint32_t **values_lengths, int key_count)
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
json_bool find_json_value_by_key(struct json_object *jobj, const char *key,
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
 * Combine values from a JSON object based on keys and a separator from another JSON object.
 *
 * @param [in] cmd_out_jobj JSON object containing the values to be combined.
 * @param [in] combine_obj JSON object specifying the keys to combine and the separator.
 * @return A dynamically allocated string containing the combined values, its the caller
 * responsibility to free the return value.
 */
char *combine_values(struct json_object *cmd_out_jobj, struct json_object *combine_obj)
{
    struct json_object *separator_obj, *values_array, *temp_val;
    const char *separator;
    char *combined_value = NULL;
    int i, combined_value_size = 0;

    // Extract separator
    if (!json_object_object_get_ex(combine_obj, "separator", &separator_obj)) {
        return NULL;
    }
    separator = json_object_get_string(separator_obj);

    if (!json_object_object_get_ex(combine_obj, "values", &values_array)) {
        return NULL;
    }
    int values_count = json_object_array_length(values_array);

    // Initial allocation for combined_value
    combined_value = malloc(1);
    combined_value[0] = '\0';
    combined_value_size = 1;

    // Iterate over the values array
    for (i = 0; i < values_count; ++i) {
        const char *value_key = json_object_get_string(json_object_array_get_idx(values_array, i));

        // Retrieve value for the current key from cmd_out_jobj
        if (find_json_value_by_key(cmd_out_jobj, value_key, &temp_val)) {
            const char *value_str = json_object_get_string(temp_val);
            combined_value_size += strlen(value_str) + (i > 0 ? strlen(separator) : 0);

            if (combined_value == NULL) {
                combined_value = malloc(combined_value_size);
            } else {
                combined_value = realloc(combined_value, combined_value_size + 1);
            }

            if (i > 0) {
                strlcat(combined_value, separator, combined_value_size + 1);
            }
            strlcat(combined_value, value_str, combined_value_size + 1);
        }
    }
    return combined_value;
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
                  const char ***key_values, uint32_t **values_lengths, int *keys_num)
{
    int ret = EXIT_SUCCESS;
    int key_count = 0;
    *key_values = NULL;
    *values_lengths = NULL;
    struct json_object *temp_value, *combine_ext_jobj = NULL;

    const struct lysc_node *child;
    for (child = list->child; child; child = child->next) {
        char *key_name = NULL, *combine_ext_str = NULL, *default_val = NULL;
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

            if (get_lys_extension(OPER_DEFAULT_VALUE_EXT, child, &default_val) == EXIT_SUCCESS) {
                if (default_val == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-default-val extension found but failed to "
                            "get the value for node \"%s\"\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }
            }

            if (get_lys_extension(OPER_COMBINE_VALUES_EXT, child, &combine_ext_str) ==
                EXIT_SUCCESS) {
                if (combine_ext_str == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-combine-values extension found but failed to "
                            "get the combined values list for node \"%s\"\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }

                combine_ext_jobj = combine_ext_str ? json_tokener_parse(combine_ext_str) : NULL;
                if (combine_ext_str != NULL) {
                    free(combine_ext_str);
                }

                if (combine_ext_jobj == NULL) {
                    fprintf(stderr,
                            "%s: Error reading schema node \"%s\" ipr2cgen:oper-stop-if extension,"
                            " the extension value has a bad json format\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }
            }

            if (json_object_object_get_ex(json_array_obj, key_name, &temp_value)) {
                // Resize the arrays to accommodate key additions
                *key_values = (const char **)realloc(*key_values, (key_count + 1) * sizeof(char *));
                *values_lengths =
                    (uint32_t *)realloc(*values_lengths, (key_count + 1) * sizeof(uint32_t));

                if (!*key_values || !*values_lengths) {
                    fprintf(stderr, "%s: Memory allocation failed\n", __func__);
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }

                char *combined_value = NULL;
                // Store the new key
                if (combine_ext_jobj != NULL) {
                    combined_value = combine_values(json_array_obj, combine_ext_jobj);
                    (*key_values)[key_count] = strdup(combined_value);
                    json_object_put(combine_ext_jobj);
                } else
                    (*key_values)[key_count] = strdup(json_object_get_string(temp_value));

                if (!(*key_values)[key_count]) {
                    fprintf(stderr, "%s: Failed to duplicate list key value\n", __func__);
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
                if (combined_value != NULL) {
                    (*values_lengths)[key_count] = strlen(combined_value);
                    free(combined_value);
                } else
                    (*values_lengths)[key_count] = strlen(json_object_get_string(temp_value));

                key_count++;
            } else {
                // key value not found in json data.
                if (default_val != NULL) {
                    // Resize the arrays to accommodate key additions
                    *key_values =
                        (const char **)realloc(*key_values, (key_count + 1) * sizeof(char *));
                    *values_lengths =
                        (uint32_t *)realloc(*values_lengths, (key_count + 1) * sizeof(uint32_t));

                    if (!*key_values || !*values_lengths) {
                        fprintf(stderr, "%s: Memory allocation failed\n", __func__);
                        ret = EXIT_FAILURE;
                        goto cleanup;
                    }
                    (*key_values)[key_count] = strdup(default_val);

                    if (!(*key_values)[key_count]) {
                        fprintf(stderr, "%s: Failed to duplicate list key value\n", __func__);
                        ret = EXIT_FAILURE;
                        goto cleanup;
                    }
                    (*values_lengths)[key_count] = strlen(default_val);
                    free(default_val);
                    key_count++;
                } else {
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
            }
            free(key_name);
        }
    }
    (*keys_num) = key_count;
    ret = EXIT_SUCCESS;
cleanup:
    return ret;
}

/* helper function convert json-c object to lyd_new_list2 keys string format */
int jobj_to_list2_keys(json_object *keys_jobj, char **keys_str)
{
    // Calculate the length of the result string
    int str_length = 1; // Start with 1 for the null terminator
    json_object_object_foreach(keys_jobj, key_alloc, val_alloc)
    {
        // Add the length of each key-value pair to the total length
        str_length +=
            snprintf(NULL, 0, "[%s=%s] ", key_alloc,
                     json_object_to_json_string_ext(val_alloc, JSON_C_TO_STRING_NOSLASHESCAPE));
    }

    // Allocate memory for the result string
    char *result_str = (char *)malloc(str_length * sizeof(char));
    if (!result_str) {
        fprintf(stderr, "%s: Failed to allocate memory\n", __func__);
        return EXIT_FAILURE;
    }
    result_str[0] = '\0';
    json_object_object_foreach(keys_jobj, key, val)
    {
        sprintf(result_str + strlen(result_str), "[%s=%s]", key,
                json_object_to_json_string_ext(val, JSON_C_TO_STRING_NOSLASHESCAPE));
    }
    *keys_str = strdup(result_str);
    free(result_str);

    return EXIT_SUCCESS;
}
/**
 * Outputs keys in the format needed for lyd_new_list2 as workaround of to libyang bug:
 * https://github.com/CESNET/libyang/pull/2207
 * 
 * Extracts keys and their values from a JSON array object based on the YANG list Schema keys.
 * This function is used to map JSON array data to YANG schema list keys.
 * @param [in] list: YANG list schema node.
 * @param [in] json_array_obj: JSON object containing the list's data.
 * @param [out] key: extracted keys string in the format [key1="val"][keyN="val"]
 * @param [out] values_lengths: Pointer to store the lengths of the keys' values.
 * @param [out] keys_num: Pointer to store the number of keys extracted.
 * @return Returns EXIT_SUCCESS if keys are successfully extracted; otherwise, returns EXIT_FAILURE.
 */
int get_list_keys2(const struct lysc_node_list *list, json_object *json_array_obj, char **keys)
{
    int ret = EXIT_SUCCESS;
    struct json_object *temp_value, *combine_ext_jobj = NULL;
    json_object *keys_jobj = json_object_new_object();
    const struct lysc_node *child;
    for (child = list->child; child; child = child->next) {
        char *key_name = NULL, *combine_ext_str = NULL, *default_val = NULL;
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
            if (get_lys_extension(OPER_DEFAULT_VALUE_EXT, child, &default_val) == EXIT_SUCCESS) {
                if (default_val == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-default-val extension found but failed to "
                            "get the value for node \"%s\"\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }
            }

            if (get_lys_extension(OPER_COMBINE_VALUES_EXT, child, &combine_ext_str) ==
                EXIT_SUCCESS) {
                if (combine_ext_str == NULL) {
                    fprintf(stderr,
                            "%s: ipr2cgen:oper-combine-values extension found but failed to "
                            "get the combined values list for node \"%s\"\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }

                combine_ext_jobj = json_tokener_parse(combine_ext_str);
                free(combine_ext_str);

                if (combine_ext_jobj == NULL) {
                    fprintf(stderr,
                            "%s: Error reading schema node \"%s\" ipr2cgen:oper-stop-if extension,"
                            " the extension value has a bad json format\n",
                            __func__, child->name);
                    return EXIT_FAILURE;
                }
            }

            if (json_object_object_get_ex(json_array_obj, key_name, &temp_value)) {
                char *value = NULL;
                if (combine_ext_jobj != NULL) {
                    value = combine_values(json_array_obj, combine_ext_jobj);
                } else
                    value = strdup(json_object_get_string(temp_value));

                json_object_object_add(keys_jobj, child->name, json_object_new_string(value));
                free(value);
            } else {
                // key value not found in json data.
                if (!strcmp(child->name, "netns")) {
                    json_object_object_add(keys_jobj, child->name,
                                           json_object_new_string(net_namespace));
                    if (default_val)
                        free(default_val);
                } else if (default_val != NULL) {
                    json_object_object_add(keys_jobj, child->name,
                                           json_object_new_string(default_val));
                    free(default_val);
                } else {
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
            }
            free(key_name);
        }
    }

    ret = jobj_to_list2_keys(keys_jobj, keys);
cleanup:
    if (keys_jobj != NULL) {
        json_object_put(keys_jobj);
    }
    if (combine_ext_jobj != NULL) {
        json_object_put(combine_ext_jobj);
    }
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
 * Evaluates if processing schema processing should terminate based on matches between `cmd_out_jobj` and 
 * `termination_obj`. It iterates through `termination_obj`, comparing its key-value pairs 
 * against those in `cmd_out_jobj`. For matching keys, it checks if any value in `termination_obj` 
 * (direct value or within an array) equals the value in `cmd_out_jobj`. Termination is advised if 
 * any match is found.
 *
 * @param [in] cmd_out_jobj: JSON object with command output to check.
 * @param [in] termination_obj: JSON object with termination criteria.
 * @return Returns true if a termination condition is met, otherwise false.
 */
bool terminate_processing(struct json_object *cmd_out_jobj, struct json_object *termination_obj)
{
    struct json_object *cmd_out_val = NULL;
    json_object_object_foreach(termination_obj, key, term_vals_obj)
    {
        if (find_json_value_by_key(cmd_out_jobj, key, &cmd_out_val)) {
            if (json_object_is_type(term_vals_obj, json_type_array)) {
                size_t n_json_values = json_object_array_length(term_vals_obj);
                for (size_t i = 0; i < n_json_values; i++) {
                    struct json_object *inner_value = json_object_array_get_idx(term_vals_obj, i);
                    if (strcmp(json_object_get_string(cmd_out_val),
                               json_object_get_string(inner_value)) == 0) {
                        return true;
                    }
                }
                if (json_object_is_type(term_vals_obj, json_type_object)) {
                    if (strcmp(json_object_get_string(cmd_out_val),
                               json_object_get_string(term_vals_obj)) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * This function compares schema node (s_node) parents names to lyd_node data tree node name, if the
 * schema node parent containers are not added to the lyd_node, this function adds them.
 * 
 * This function is to be used before processing leafs and leaf_lists to ensure their parent containers
 * are added to the lyd data tree.
 * 
 * @param [in] s_node: schema node being processed (typically the s_node of a leaf or leaf_list).
 * @param [in] parent_data_node: lyd data tree to which nodes under process are being attached to.
 */
int add_missing_parents(const struct lysc_node *s_node, struct lyd_node **parent_data_node)
{
    if (*parent_data_node == NULL) {
        fprintf(stderr, "%s: Failed to check node parents, reference lyd_node is NULL\n", __func__);
        return EXIT_FAILURE;
    }

    struct lyd_node *new_data_node = NULL;
    char snode_parent_xpath[1024] = { 0 };
    char parent_data_node_xpath[1024] = { 0 };
    lysc_path(s_node->parent, LYSC_PATH_LOG, snode_parent_xpath, 1024);
    lysc_path((*parent_data_node)->schema, LYSC_PATH_LOG, parent_data_node_xpath, 1024);

    /* exit if root node was reached */
    if (strcmp(snode_parent_xpath, "/") == 0 || strcmp(parent_data_node_xpath, "/") == 0)
        return EXIT_SUCCESS;

    /* Check first-level children in case parent was added by a sibling leaf/leaf-list iteration */
    struct lyd_node *child_node = NULL;
    LY_LIST_FOR(lyd_child(*parent_data_node), child_node)
    {
        if (child_node->schema->nodetype == LYS_CONTAINER) {
            lysc_path(child_node->schema, LYSC_PATH_LOG, parent_data_node_xpath, 1024);
            if (strcmp(snode_parent_xpath, parent_data_node_xpath) == 0) {
                *parent_data_node = child_node;
                return EXIT_SUCCESS;
            }
        }
    }

    /* check parent xpaths */
    if (strcmp(snode_parent_xpath, parent_data_node_xpath) != 0 &&
        s_node->parent->nodetype == LYS_CONTAINER) {
        /* check grand_parents xpaths */
        add_missing_parents(s_node->parent, parent_data_node);

        /* add missing parent */
        if (LY_SUCCESS ==
            lyd_new_inner(*parent_data_node, NULL, s_node->parent->name, 0, &new_data_node)) {
            *parent_data_node = new_data_node;
        }
    }
    return EXIT_SUCCESS;
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
    if (value == NULL) {
        json_object *flag_unset_obj = NULL;
        json_object_object_get_ex(fmap_jobj, "FLAG-UNSET", &flag_unset_obj);
        value = json_object_get_string(flag_unset_obj);
    }
    if (LY_SUCCESS != lyd_new_term(*parent_data_node, NULL, s_node->name, value, 0, NULL)) {
        fprintf(stderr, "%s: node %s creation failed\n", __func__, s_node->name);
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
    char *vmap_str = NULL, *fmap_str = NULL, *combine_ext_str = NULL, *static_value = NULL;
    if (!strcmp(s_node->name, "netns")) {
        if (LY_SUCCESS !=
            lyd_new_term(*parent_data_node, NULL, s_node->name, net_namespace, 0, NULL)) {
            fprintf(stderr, "%s: node %s creation failed\n", __func__, s_node->name);
            return;
        }
    }
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

    if (get_lys_extension(OPER_COMBINE_VALUES_EXT, s_node, &combine_ext_str) == EXIT_SUCCESS) {
        if (combine_ext_str == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-combine-values extension found but failed to "
                    "get the combined values list for node \"%s\"\n",
                    __func__, s_node->name);
            return;
        }
    }
    struct json_object *combine_ext_jobj = combine_ext_str ? json_tokener_parse(combine_ext_str) :
                                                             NULL;
    if (combine_ext_str != NULL) {
        free(combine_ext_str);
        if (combine_ext_jobj == NULL) {
            fprintf(stderr,
                    "%s: Error reading schema node \"%s\" ipr2cgen:oper-combine-value extension,"
                    " the extension value has a bad json format\n",
                    __func__, s_node->name);
            return;
        }
    }

    struct json_object *fmap_jobj = fmap_str ? strmap_to_jsonmap(fmap_str) : NULL;
    if (fmap_str != NULL) {
        free(fmap_str);
        if (fmap_jobj == NULL) {
            fprintf(stderr,
                    "%s: Error reading schema node \"%s\" ipr2cgen:oper-flag-map extension,"
                    " the extension value has a bad format\n",
                    __func__, s_node->name);
            return;
        }
    }

    struct json_object *vmap_jobj = vmap_str ? strmap_to_jsonmap(vmap_str) : NULL;
    if (vmap_str != NULL) {
        free(vmap_str);
        if (vmap_jobj == NULL) {
            fprintf(stderr,
                    "%s: Error reading schema node \"%s\" ipr2cgen:oper-value-map extension,"
                    " the extension value has a bad format\n",
                    __func__, s_node->name);
            return;
        }
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
                fprintf(stderr, "%s: node %s creation failed\n", __func__, s_node->name);
                free(static_value);
                return;
            }
            free(static_value);
        } else if (temp_obj) {
            add_missing_parents(s_node, parent_data_node);
            if (json_object_is_type(temp_obj, json_type_array) && fmap_jobj) {
                // array values are processed as flags.
                flags_to_leafs(temp_obj, fmap_jobj, parent_data_node, s_node);
            } else {
                // Process a single value
                if (combine_ext_jobj != NULL) {
                    char *combined_value = combine_values(json_obj, combine_ext_jobj);
                    if (LY_SUCCESS != lyd_new_term(*parent_data_node, NULL, s_node->name,
                                                   combined_value, 0, NULL)) {
                        fprintf(stderr, "%s: node %s creation failed\n", __func__, s_node->name);
                    }
                    free(combined_value);
                } else {
                    const char *value =
                        map_value_if_needed(vmap_jobj, json_object_get_string(temp_obj));
                    if (LY_SUCCESS !=
                        lyd_new_term(*parent_data_node, NULL, s_node->name, value, 0, NULL)) {
                        fprintf(stderr, "%s: node %s creation failed\n", __func__, s_node->name);
                    }
                }
            }
        }
    }
cleanup:
    if (combine_ext_jobj != NULL) {
        json_object_put(combine_ext_jobj);
    }
    if (vmap_jobj != NULL) {
        json_object_put(vmap_jobj);
    }
    if (fmap_jobj != NULL) {
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

/*
This function shouldn't be used.
Uses lyd_new_list3, on libyang version 2.1.148 there is a bug:
https://github.com/CESNET/libyang/pull/2207
*/
void single_jobj_to_list(struct json_object *json_obj, struct lyd_node **parent_data_node,
                         const struct lysc_node *s_node, uint16_t lys_flags)
{
    const struct lysc_node_list *list = (const struct lysc_node_list *)s_node;
    const char **key_values;
    uint32_t *values_lengths;
    int keys_count;
    if (get_list_keys(list, json_obj, &key_values, &values_lengths, &keys_count) == EXIT_SUCCESS) {
        add_missing_parents(s_node, parent_data_node);
        struct lyd_node *new_data_node = NULL;
        if (LY_SUCCESS != lyd_new_list3(*parent_data_node, NULL, s_node->name, key_values,
                                        values_lengths, 0, &new_data_node)) {
            fprintf(stderr, "%s: list \"%s\" creation failed.\n", __func__, s_node->name);
            free_list_params(&key_values, &values_lengths, keys_count);
            return;
        }

        free_list_params(&key_values, &values_lengths, keys_count);

        if (!new_data_node)
            new_data_node = *parent_data_node;

        // Recursively process child nodes
        const struct lysc_node *s_child;
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            if (process_node(s_child, json_obj, lys_flags, &new_data_node))
                return;
        }
    }
}

/* 
Uses lyd_new_list2
*/
void single_jobj_to_list2(struct json_object *json_obj, struct lyd_node **parent_data_node,
                          const struct lysc_node *s_node, uint16_t lys_flags)
{
    const struct lysc_node_list *list = (const struct lysc_node_list *)s_node;
    char *keys = NULL;
    if (get_list_keys2(list, json_obj, &keys) == EXIT_SUCCESS) {
        if (add_missing_parents(s_node, parent_data_node) == EXIT_FAILURE)
            return;
        struct lyd_node *new_data_node = NULL;
        if (LY_SUCCESS !=
            lyd_new_list2(*parent_data_node, NULL, s_node->name, keys, 0, &new_data_node)) {
            fprintf(stderr, "%s: list \"%s\" creation failed.\n", __func__, s_node->name);
            //free_list_params(&key_values, &values_lengths, keys_count);
            free(keys);
            return;
        }
        //free_list_params(&key_values, &values_lengths, keys_count);

        if (!new_data_node)
            new_data_node = *parent_data_node;

        // Recursively process child nodes
        const struct lysc_node *s_child;
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            if (process_node(s_child, json_obj, lys_flags, &new_data_node))
                return;
        }
    }
    free(keys);
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
    // json_obj itself is an array of objects
    if (json_object_get_type(json_obj) == json_type_array) {
        size_t n_arrays = json_object_array_length(json_obj);
        for (size_t i = 0; i < n_arrays; i++) {
            struct json_object *arr_obj = json_object_array_get_idx(json_obj, i);
            jdata_to_list(arr_obj, arg_name, s_node, lys_flags, parent_data_node);
        }
    }
    // json_obj itself is single object but its content is an array
    struct json_object *lists_arrays;
    if (find_json_value_by_key(json_obj, arg_name, &lists_arrays) &&
        json_object_get_type(lists_arrays) == json_type_array) {
        size_t n_arrays = json_object_array_length(lists_arrays);
        for (size_t i = 0; i < n_arrays; i++) {
            struct json_object *list_obj = json_object_array_get_idx(lists_arrays, i);
            single_jobj_to_list2(list_obj, parent_data_node, s_node, lys_flags);
        }
    } else {
        // json_obj itself is single object and the object content is the list leafs
        single_jobj_to_list2(json_obj, parent_data_node, s_node, lys_flags);
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
int process_node(const struct lysc_node *s_node, json_object *json_obj, uint16_t lys_flags,
                 struct lyd_node **parent_data_node)
{
    const struct lysc_node *s_child;
    struct lyd_node *new_data_node = *parent_data_node;

    /* check for schema extension overrides */
    char *arg_name = NULL;
    char *term_vals = NULL;
    char *sub_jobj_name = NULL;
    json_object *node_jobj = NULL;

    if (get_lys_extension(OPER_STOP_IF_EXT, s_node, &term_vals) == EXIT_SUCCESS) {
        if (term_vals == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-arg-name extension found but failed to "
                    "get the arg-name value for node \"%s\"\n",
                    __func__, s_node->name);
            return EXIT_FAILURE;
        }

        struct json_object *term_jobj = term_vals ? json_tokener_parse(term_vals) : NULL;
        if (term_vals) {
            free(term_vals);
        }

        if (term_jobj == NULL) {
            fprintf(stderr,
                    "%s: Error reading schema node \"%s\" ipr2cgen:oper-stop-if extension,"
                    " the extension value has a bad json format\n",
                    __func__, s_node->name);
            return EXIT_FAILURE;
        }

        if (terminate_processing(json_obj, term_jobj)) {
            s_node = s_node->next;
            return EXIT_SUCCESS;
        }
    }

    if (get_lys_extension(OPER_ARG_NAME_EXT, s_node, &arg_name) == EXIT_SUCCESS) {
        if (arg_name == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-arg-name extension found but failed to "
                    "get the arg-name value for node \"%s\"\n",
                    __func__, s_node->name);
            return EXIT_FAILURE;
        }
    } else {
        arg_name = strdup(s_node->name);
    }

    if (get_lys_extension(OPER_SUB_JOBJ_EXT, s_node, &sub_jobj_name) == EXIT_SUCCESS) {
        if (sub_jobj_name == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-sub-job extension found but failed to "
                    "get the sub json object name value for node \"%s\"\n",
                    __func__, s_node->name);
            return EXIT_FAILURE;
        }
    }
    if (sub_jobj_name != NULL) {
        find_json_value_by_key(json_obj, sub_jobj_name, &node_jobj);
        free(sub_jobj_name);
    } else {
        node_jobj = json_obj;
    }

    switch (s_node->nodetype) {
    case LYS_LEAFLIST:
    case LYS_LEAF: {
        /* json keys are added on list creation, don't re-add them */
        if (lysc_is_key(s_node))
            break;

        /* stop processing if s_node flags doesn't include the requested "config true|false" flags */
        if ((s_node->flags & LYS_CONFIG_W) && !(lys_flags & LYS_CONFIG_W))
            break;
        if ((s_node->flags & LYS_CONFIG_R) && !(lys_flags & LYS_CONFIG_R))
            break;

        if (s_node->nodetype == LYS_LEAFLIST)
            jdata_to_leaflist(node_jobj, arg_name, parent_data_node, s_node);

        if (s_node->nodetype == LYS_LEAF)
            jdata_to_leaf(node_jobj, arg_name, parent_data_node, s_node);
        break;
    }
    case LYS_LIST: {
        jdata_to_list(node_jobj, arg_name, s_node, lys_flags, parent_data_node);
        break;
    }
    case LYS_CHOICE:
    case LYS_CASE:
    case LYS_CONTAINER: {
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            if (process_node(s_child, node_jobj, lys_flags, &new_data_node))
                return EXIT_FAILURE;
        }
        break;
    }
    default:
        break;
    }
    free(arg_name);
    return EXIT_SUCCESS;
}

int merge_json_by_key(struct json_object *dest_jobj, struct json_object *src_array, char *key,
                      char *include_key)
{
    // Get the key value from the outer JSON object
    const char *outter_jkey = json_object_get_string(json_object_object_get(dest_jobj, key));
    if (!outter_jkey) {
        return -1; // Handle the case where the key is not found
    }

    // Iterate through the inner array
    size_t inner_array_length = json_object_array_length(src_array);
    for (size_t i = 0; i < inner_array_length; i++) {
        struct json_object *inner_obj = json_object_array_get_idx(src_array, i);
        const char *inner_jkey = json_object_get_string(json_object_object_get(inner_obj, key));

        // If keys match, merge the objects
        if (strcmp(outter_jkey, inner_jkey) == 0) {
            struct json_object *include_obj = json_object_object_get(inner_obj, include_key);
            if (!include_obj) {
                return -1; // Handle the case where the include_key is not found
            }

            // Deep copy the include_obj
            struct json_object *include_obj_copy = NULL;
            json_object_deep_copy(include_obj, &include_obj_copy, NULL);
            if (!include_obj_copy) {
                return -1; // Handle deep copy failure
            }

            // Add the copied object to the outer JSON object
            json_object_object_add(dest_jobj, include_key, include_obj_copy);
            break;
        }
    }

    return 0;
}

/**
 * Starts the processing of module schema, it processes every node in the schema to lyd_node if the node name
 * is found in the input json_obj.
 * First it looks for show commands to apply as it iterates through the schema node, then it stores the outputs
 * into json_obj, and starts processing the schema nodes based on the content of that json_obj.
 * @param [in] s_node: Schema node to be processed.
 * @param [in] json_obj: JSON object containing the data to process.
 * @param [in] lys_flags: flag value used to filter out schema nodes containers and leafs.
 *                        Use LYS_CONFIG_R to allow parsing read-only containers and leafs (for loading operational data).
 *                        Use LYS_CONFIG_W to allow parsing write containers and leafs (for loading configuration data).
 * @param [in, out] parent_data_node: Pointer to the parent data node in the YANG data tree (lyd_node).
 */
int process_schema(const struct lysc_node *s_node, uint16_t lys_flags,
                   struct lyd_node **parent_data_node)
{
    // before processing the schema, check if it's flags match the requested "lys_flags"
    if (s_node->flags & LYS_CONFIG_R) {
        if (!(lys_flags & s_node->flags))
            return EXIT_SUCCESS;
    }
    char *show_cmd = NULL;
    char *tc_filter_type = NULL;
    char *json_buffer_cpy = NULL;
    struct json_object *cmd_output = NULL;

    /* Create top-level lyd_node */
    if (*parent_data_node == NULL) { // Top-level node
        lyd_new_inner(NULL, s_node->module, s_node->name, 0, parent_data_node);
    }

    if (get_lys_extension(OPER_CMD_EXT, s_node, &show_cmd) == EXIT_SUCCESS) {
        if (show_cmd == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:oper-cmd extension found but failed to "
                    "get the command value for node \"%s\"\n",
                    __func__, s_node->name);
            return EXIT_FAILURE;
        }
        if (strcmp(net_namespace, "1") != 0) {
            // Calculate the new size needed for show_cmd
            size_t new_size = strlen(show_cmd) + strlen(" -n ") + strlen(net_namespace) +
                              1; // +1 for the null terminator

            // Reallocate memory for show_cmd
            show_cmd = realloc(show_cmd, new_size);
            if (show_cmd == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            insert_netns(show_cmd, net_namespace);
        }
        if (apply_ipr2_cmd(show_cmd) != EXIT_SUCCESS) {
            fprintf(stderr, "%s: command execution failed\n", __func__);

            return EXIT_FAILURE;
        }
        free(show_cmd);

        json_buffer_cpy = strdup(json_buffer);
        cmd_output = json_tokener_parse(json_buffer_cpy);

        struct json_object *inner_cmd_output = NULL;
        char *inner_cmd_arg, *json_buffer_inner_cpy = NULL;
        char *inner_show_cmd = NULL, *inner_cmd_key = NULL, *inner_cmd_inculde_key = NULL;
        if (get_lys_extension(OPER_INNER_CMD_EXT, s_node, &inner_cmd_arg) == EXIT_SUCCESS) {
            char *temp = NULL;
            char *token = NULL;
            // Create a copy of the input string to avoid modifying the original string
            temp = strdup(inner_cmd_arg);

            // Tokenize the string using comma as the delimiter
            token = strtok(temp, ",");
            if (token) {
                inner_show_cmd = strdup(token);
                token = strtok(NULL, ",");
            }
            if (token) {
                inner_cmd_key = strdup(token);
                token = strtok(NULL, ",");
            }
            if (token) {
                inner_cmd_inculde_key = strdup(token);
            }
            // Free the duplicated string
            free(temp);
            // Check if all tokens were found
            if (!inner_show_cmd || !inner_cmd_key || !inner_cmd_inculde_key) {
                fprintf(stderr, "%s: failed to get inner_show_cmd ext argument for node = %s\n",
                        __func__, s_node->name);
                return EXIT_FAILURE;
            }
            if (strcmp(net_namespace, "1") != 0) {
                // Calculate the new size needed for show_cmd
                size_t new_size = strlen(inner_show_cmd) + strlen(" -n ") + strlen(net_namespace) +
                                  1; // +1 for the null terminator

                // Reallocate memory for show_cmd
                inner_show_cmd = realloc(inner_show_cmd, new_size);
                insert_netns(inner_show_cmd, net_namespace);
            }

            if (apply_ipr2_cmd(inner_show_cmd) != EXIT_SUCCESS) {
                fprintf(stderr, "%s: command execution failed\n", __func__);
                return EXIT_FAILURE;
            }
            free(inner_show_cmd);
            json_buffer_inner_cpy = strdup(json_buffer);
            inner_cmd_output = json_tokener_parse(json_buffer_inner_cpy);
        }

        if (json_object_get_type(cmd_output) == json_type_array) {
            // Iterate over json_obj arrays:
            size_t n_arrays = json_object_array_length(cmd_output);
            for (size_t i = 0; i < n_arrays; i++) {
                struct json_object *array_obj = json_object_array_get_idx(cmd_output, i);

                if (inner_cmd_output) {
                    merge_json_by_key(array_obj, inner_cmd_output, inner_cmd_key,
                                      inner_cmd_inculde_key);
                }
                process_node(s_node, array_obj, lys_flags, parent_data_node);
            }
        }
        if (inner_cmd_output)
            json_object_put(inner_cmd_output);
        if (json_buffer_inner_cpy)
            free(json_buffer_inner_cpy);

    } else if (get_lys_extension(OPER_DUMP_TC_FILTERS, s_node, &tc_filter_type) == EXIT_SUCCESS) {
        int tc_command_count = 0;
        char tc_cmd_key_value[CMD_LINE_SIZE];
        char *tc_cmd_key_name = NULL, *tc_filter_direction = NULL;
        struct json_object *qdisc_cmd_output = NULL, *tc_cmd_output = NULL;
        char **tc_commands = malloc(CMDS_ARRAY_SIZE * sizeof(char *));

        if (tc_commands == NULL) {
            fprintf(stderr, "%s: Failed to allocate memory for tc_commands array", __func__);
            return EXIT_FAILURE;
        }

        for (int i = 0; i < CMDS_ARRAY_SIZE; i++) {
            tc_commands[i] = malloc(CMD_LINE_SIZE * sizeof(char));
            if (tc_commands[i] == NULL) {
                fprintf(stderr, "%s: Failed to allocate memory for a command string", __func__);
                return EXIT_FAILURE;
            }
        }

        /* Apply tc qdisc command, qdisc outputs are used to generate tc filters commands */
        char *tc_qdisc_cmd = "tc qdisc list";
        if (strcmp(net_namespace, "1") != 0) {
            tc_qdisc_cmd = "tc qdisc list -n %s";
        }
        if (apply_ipr2_cmd(tc_qdisc_cmd) != EXIT_SUCCESS) {
            fprintf(stderr, "%s: command execution failed\n", __func__);
            free(tc_filter_type);
            return EXIT_FAILURE;
        }
        qdisc_cmd_output = json_tokener_parse(json_buffer);
        if (json_object_get_type(qdisc_cmd_output) == json_type_array) {
            qdiscs_to_filters_cmds(qdisc_cmd_output, tc_filter_type, tc_commands,
                                   &tc_command_count);
        }
        free(tc_filter_type);
        if (qdisc_cmd_output)
            json_object_put(qdisc_cmd_output);

        /* Iterate over generated tc filter commands */
        for (int i = 0; i < tc_command_count; i++) {
            char *filter_list_str = NULL;
            struct lyd_node *new_filter = NULL;

            /* Extract tc list keys */
            if (strstr(tc_commands[i], "block")) {
                sscanf(tc_commands[i], "tc filter show block %s", tc_cmd_key_value);
                tc_cmd_key_name = "block";
                tc_filter_direction = NULL;
            } else if (strstr(tc_commands[i], "dev")) {
                sscanf(tc_commands[i], "tc filter show dev %s", tc_cmd_key_value);
                tc_cmd_key_name = "dev";

                if (strstr(tc_commands[i], "ingress"))
                    tc_filter_direction = "ingress";
                else if (strstr(tc_commands[i], "egress"))
                    tc_filter_direction = "egress";
                else
                    tc_filter_direction = NULL;
            }

            /* Create tc list json-c object */
            json_object *filter_list_jobj = json_object_new_object();
            json_object_object_add(filter_list_jobj, tc_cmd_key_name,
                                   json_object_new_string(tc_cmd_key_value));
            json_object_object_add(filter_list_jobj, "netns",
                                   json_object_new_string(net_namespace));
            if (tc_filter_direction != NULL)
                json_object_object_add(filter_list_jobj, "direction",
                                       json_object_new_string(tc_filter_direction));

            /* Apply tc filter command */
            if (apply_ipr2_cmd(tc_commands[i]) != EXIT_SUCCESS) {
                fprintf(stderr, "%s: command execution failed\n", __func__);
                free(tc_commands[i]);
                free(tc_commands);
                return EXIT_FAILURE;
            }
            free(tc_commands[i]);

            /* Process tc filter rules */
            tc_cmd_output = json_tokener_parse(json_buffer);
            if (json_object_get_type(tc_cmd_output) == json_type_array) {
                /* Process only if the command outputs are not empty */
                if (json_object_array_length(tc_cmd_output) != 0) {
                    /* Create the tc list lyd_node */
                    jobj_to_list2_keys(filter_list_jobj, &filter_list_str);
                    lyd_new_list2(*parent_data_node, NULL, s_node->name, filter_list_str, 0,
                                  &new_filter);
                    json_object_put(filter_list_jobj);
                    free(filter_list_str);

                    /* start processing the filter rules */
                    size_t n_arrays = json_object_array_length(tc_cmd_output);
                    for (size_t i = 0; i < n_arrays; i++) {
                        if (i % 2 == 1) {
                            struct json_object *tc_array_obj =
                                json_object_array_get_idx(tc_cmd_output, i);
                            const struct lysc_node *s_child = NULL;
                            LY_LIST_FOR(lysc_node_child(s_node), s_child)
                            {
                                process_node(s_child, tc_array_obj, lys_flags, &new_filter);
                            }
                        }
                    }
                }
            }
            if (tc_cmd_output)
                json_object_put(tc_cmd_output);
        }
        free(tc_commands);
    } else {
        const struct lysc_node *s_child;
        LY_LIST_FOR(lysc_node_child(s_node), s_child)
        {
            process_schema(s_child, lys_flags, parent_data_node);
        }
    }
    if (cmd_output)
        json_object_put(cmd_output);
    if (json_buffer_cpy)
        free(json_buffer_cpy);

    return EXIT_SUCCESS;
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
int load_module_data(sr_session_ctx_t *session, const char *module_name, uint16_t lys_flags,
                     struct lyd_node **parent, char *nsname)
{
    int ret = SR_ERR_OK;
    const struct ly_ctx *ly_ctx;
    const struct lys_module *module = NULL;
    struct lyd_node *data_tree = NULL;
    net_namespace = nsname;

    ly_ctx = sr_acquire_context(sr_session_get_connection(session));
    module = ly_ctx_get_module_implemented(ly_ctx, module_name);
    if (module == NULL) {
        fprintf(stderr, "%s: Failed to get requested module schema, module name: %s\n", __func__,
                module_name);
        ret = SR_ERR_CALLBACK_FAILED;
        goto cleanup;
    }

    const struct lysc_node *node;
    LY_LIST_FOR(module->compiled->data, node)
    {
        // Start with level 0 for top-level nodes, data_tree = NULL
        //process_node(node, json_array_obj, lys_flags, data_tree);
        process_schema(node, lys_flags, &data_tree);
        if (lyd_merge_tree(parent, data_tree, LYD_MERGE_DEFAULTS)) {
            /* Merge of one json_array data_tree failed.
               This is a partial failure, no need to return ERR. */
            fprintf(stderr, "%s: Partial failure on pushing '%s' operational data\n", __func__,
                    data_tree->schema->name);
        }
        // Free data_tree after merging it.
        lyd_free_tree(data_tree);
        data_tree = NULL;
    }

cleanup:
    sr_release_context(sr_session_get_connection(session));
    return ret;
}
