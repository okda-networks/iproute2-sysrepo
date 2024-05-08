/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Authors:     Ali Aqrabawi, <aaqrbaw@okdanetworks.com>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU Affero General Public
 *              License Version 3.0 as published by the Free Software Foundation;
 *              either version 3.0 of the License, or (at your option) any later
 *              version.
 *
 * Copyright (C) 2024 Okda Networks, <aaqrbaw@okdanetworks.com>
 */

#include <ctype.h>

#include "cmdgen.h"

extern sr_session_ctx_t *sr_session;
static int processed = 1;
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
 * all details in yang/iproute2-cmdgen-extensions.yang
 */
typedef enum {
    // list extensions
    CMD_START_EXT,
    GROUP_LIST_WITH_SEPARATOR_EXT,
    GROUP_LEAFS_VALUES_SEPARATOR_EXT,
    INCLUDE_PARENT_LEAFS,

    // root container extensions
    CMD_ADD_EXT,
    CMD_DELETE_EXT,
    CMD_UPDATE_EXT,

    // leaf extensions
    ARG_NAME_EXT,
    FLAG_EXT,
    VALUE_ONLY_EXT,
    VALUE_ONLY_ON_UPDATE_EXT,
    ADD_LEAF_AT_END,

    AFTER_NODE_ADD_STATIC_ARG_EXT,
    ON_NODE_DELETE_EXT,

    //other
    ON_UPDATE_INCLUDE_EXT,
    ADD_STATIC_ARG_EXT,
    REPLACE_ON_UPDATE_EXT,
    INCLUDE_ALL_ON_DELETE,

} extension_t;

char *yang_ext_map[] = { [CMD_START_EXT] = "cmd-start",
                         [CMD_ADD_EXT] = "cmd-add",
                         [CMD_DELETE_EXT] = "cmd-delete",
                         [CMD_UPDATE_EXT] = "cmd-update",
                         [GROUP_LIST_WITH_SEPARATOR_EXT] = "group-list-with-separator",
                         [GROUP_LEAFS_VALUES_SEPARATOR_EXT] = "group-leafs-values-separator",
                         [INCLUDE_PARENT_LEAFS] = "include_parent_leafs",

                         // leaf extensions
                         [ARG_NAME_EXT] = "arg-name",
                         [FLAG_EXT] = "flag",
                         [VALUE_ONLY_EXT] = "value-only",
                         [VALUE_ONLY_ON_UPDATE_EXT] = "value-only-on-update",
                         [AFTER_NODE_ADD_STATIC_ARG_EXT] = "after-node-add-static-arg",
                         [ON_NODE_DELETE_EXT] = "on-node-delete",
                         [ADD_LEAF_AT_END] = "add_leaf_at_end",

                         // other
                         [ON_UPDATE_INCLUDE_EXT] = "on-update-include",
                         [ADD_STATIC_ARG_EXT] = "add-static-arg",
                         [REPLACE_ON_UPDATE_EXT] = "replace-on-update",
                         [INCLUDE_ALL_ON_DELETE] = "include-all-on-delete" };

void dup_argv(char ***dest, char **src, int argc)
{
    *dest = (char **)malloc((argc + 1) * sizeof(char *));
    if (*dest == NULL) {
        fprintf(stderr, "%s: Memory allocation failed\n", __func__);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < argc; i++) {
        (*dest)[i] = strdup(src[i]);
        if ((*dest)[i] == NULL) {
            fprintf(stderr, "%s: Memory allocation failed\n", __func__);
            exit(EXIT_FAILURE);
        }
    }
    (*dest)[argc] = NULL; // NULL terminator
}

void free_argv(char **argv, int argc)
{
    // Free memory for each string
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
        argv[i] = NULL;
    }
    // Free memory for the array of pointers
    free(argv);
}

void free_cmds_info(struct cmd_info **cmds_info)
{
    // Free the memory allocated for cmds
    for (int i = 0; i < CMDS_ARRAY_SIZE; i++) {
        if (cmds_info[i] != NULL) {
            // Free the argv array for cmd_args[i]
            for (int j = 0; j < cmds_info[i]->argc; j++) {
                free(cmds_info[i]->argv[j]);
            }
            for (int j = 0; j < cmds_info[i]->rollback_argc; j++) {
                free(cmds_info[i]->rollback_argv[j]);
            }
            // Free the argv array itself
            free(cmds_info[i]->argv);
            free(cmds_info[i]->rollback_argv);
            // Free the cmd_args struct itself
            free(cmds_info[i]);
        }
    }
    // Free the cmds array itself
    free(cmds_info);
}

/**
 * @brief this function will extract static_arg and xpath_arg from string input,
 * example: if input = "dev (../../name)" -result-> static = dev, and xpath = ../../name
 *          anything added between '(' ')' will be recognized as xpath_arg
 * @param input [in] input string: "dev" or "dev (../../name)"
 * @param static_arg [out] extracted static arg, e.a "dev"
 * @param xpath_arg [out]  extracted xpath arg, e.a "../../name" if there is any
 */
void extract_static_and_xpath_args(char *input, char **static_arg, char **xpath_arg)
{
    // Find the position of '(' and ')'
    char *start = strchr(input, '(');
    char *end = strrchr(input, ')');

    if (start != NULL && end != NULL && start < end) {
        // If '(' and ')' are found and '(' comes before ')', treat as static (first) and dynamic (second) parts
        size_t first_len = start - input;
        size_t second_len = end - start - 1;

        *static_arg = malloc(first_len + 1);
        *xpath_arg = malloc(second_len + 1);
        if (*static_arg == NULL || *xpath_arg == NULL) {
            printf("Memory allocation failed\n");
            exit(1);
        }

        strncpy(*static_arg, input, first_len);
        (*static_arg)[first_len] = '\0';
        strncpy(*xpath_arg, start + 1, second_len);
        (*xpath_arg)[second_len] = '\0';
    } else {
        // If '(' and ')' are not found or '(' comes after ')', treat the whole string as the first part
        size_t input_len = strlen(input);
        *static_arg = malloc(input_len + 1);
        if (*static_arg == NULL) {
            printf("Memory allocation failed\n");
            exit(1);
        }

        strncpy(*static_arg, input, input_len);
        (*static_arg)[input_len] = '\0';
        *xpath_arg = NULL; // No second part
    }
}

/**
 * if input is "iproute2-ip-link:dummy" it return "dummy"
 * @param [in] input string to be stripped.
 * @return the extracted string
 */
char *strip_yang_iden_prefix(const char *input)
{
    const char *colon_pos = strchr(input, ':');
    if (colon_pos != NULL) {
        // Calculate the length of the substring after the colon
        size_t len = strlen(colon_pos + 1);

        // Allocate memory for output string
        char *output = malloc(len + 1);
        if (output == NULL) {
            fprintf(stderr, "%s: Memory allocation failed\n", __func__);
            exit(EXIT_FAILURE);
        }

        // Copy the substring after the colon.
        strlcpy(output, colon_pos + 1, len + 1);

        // Remove leading and trailing whitespace
        char *end = output + len - 1;
        while (end > output && isspace((unsigned char)*end)) {
            *end-- = '\0';
        }
        while (*output && isspace((unsigned char)*output)) {
            ++output;
        }
        return output;
    } else {
        // No colon found, return the original string
        return strdup(input);
    }
}

/**
 * get the yang operation from the lyd_node (create, replace, delete)
 * @param [in]dnode lyd_node
 * @return oper_t the time of the operation found on this lyd_node.
 */
oper_t get_operation(const struct lyd_node *dnode)
{
    struct lyd_meta *dnode_meta = NULL;
    const char *operation;
    dnode_meta = lyd_find_meta(dnode->meta, NULL, "yang:operation");
    if (dnode_meta == NULL)
        return UNKNOWN_OPR;
    operation = lyd_get_meta_value(dnode_meta);
    if (!strcmp("create", operation))
        return ADD_OPR;
    if (!strcmp("delete", operation))
        return DELETE_OPR;
    if (!strcmp("replace", operation) ||
        !strcmp("none", operation)) // for updated list the operation is none.
        return UPDATE_OPR;
    return UNKNOWN_OPR;
}

/**
 * get the extension from lyd_node,
 * @param [in] ex_t extension_t to be captured from lyd_node.
 * @param [in] dnode lyd_node where to search for provided ext.
 * @param [out] value extension value if found. can be null.
 * @return EXIT_SUCCESS if ext found, EXIT_FAILURE if not found
 */
int get_extension(extension_t ex_t, const struct lyd_node *dnode, char **value)
{
    struct lysc_ext_instance *ys_extenstions = dnode->schema->exts;
    if (ys_extenstions == NULL)
        return EXIT_FAILURE;

    LY_ARRAY_COUNT_TYPE i;
    LY_ARRAY_FOR(ys_extenstions, i)
    {
        if (!strcmp(ys_extenstions[i].def->name, yang_ext_map[ex_t])) {
            if (value != NULL)
                *value = strdup(ys_extenstions[i].argument);
            return EXIT_SUCCESS;
        }
    }
    return EXIT_FAILURE;
}

/**
 * check if this node is cmd-start node.
 * example in yang the list nexthop is where the cmd-start ext added.
 * @param [in] dnode lyd_node
 * @return 1 if ext found.
 */
int is_startcmd_node(struct lyd_node *dnode)
{
    if (get_extension(CMD_START_EXT, dnode, NULL) == EXIT_SUCCESS)
        return 1;
    return 0;
}

struct lyd_node *get_parent_startcmd(struct lyd_node *dnode)
{
    struct lyd_node *startcmd = dnode;
    while (startcmd) {
        if (is_startcmd_node(startcmd))
            break;
        startcmd = lyd_parent(startcmd);
    }
    return startcmd;
}

/**
 * parse the command line and convert it to argc, argv
 * @param [in]  command command line string "ip link add ..."
 * @param [out] argc parsed argv count
 * @param [out] argv parsed argv
 */
void parse_command(const char *command, int *argc, char ***argv)
{
    // Count the number of space-separated tokens to determine argc
    *argc = 1;
    for (const char *ptr = command; *ptr; ++ptr) {
        if (*ptr == ' ') {
            (*argc)++;
            // Skip consecutive spaces
            while (*(ptr + 1) == ' ') {
                ptr++;
            }
        }
    }

    // Allocate memory for argv
    *argv = (char **)malloc((*argc + 1) * sizeof(char *));
    if (*argv == NULL) {
        fprintf(stderr, "%s: Memory allocation failed\n", __func__);
        exit(EXIT_FAILURE);
    }

    // Tokenize the command string and populate argv
    char *token;
    int i = 0;
    char *cmd_copy =
        strdup(command); // We duplicate the command string as strtok modifies the original string
    token = strtok(cmd_copy, " ");
    while (token != NULL) {
        (*argv)[i] = strdup(token);
        token = strtok(NULL, " ");
        i++;
    }
    (*argv)[*argc] = NULL; // NULL terminator
    free(cmd_copy);
}

/**
 * this function take cmd_line string, convert it to argc, argv and then append it to the cmds array
 *  @param [in,out] cmds array of cmd_info.
 * @param [in,out] cmd_idx current index of cmds.
 * @param [in] cmd_line command line string "ip link add name lo0 type dummy"
 * @param [in] cmd_line_rb the rollback command line string.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int add_command(struct cmd_info **cmds, int *cmd_idx, char *cmd_line, char *cmd_line_rb)
{
    int argc, argc_rb;
    char **argv, **argv_rb;
    if (*cmd_idx >= CMDS_ARRAY_SIZE) {
        fprintf(stderr,
                "%s: cmds exceeded MAX allowed commands per transaction, MAX_ALLOWED_CMDS=%d\n",
                __func__, CMDS_ARRAY_SIZE);
        return EXIT_FAILURE;
    }
    parse_command(cmd_line, &argc, &argv);
    parse_command(cmd_line_rb, &argc_rb, &argv_rb);

    (cmds)[*cmd_idx] = malloc(sizeof(struct cmd_info));

    if ((cmds)[*cmd_idx] == NULL) {
        fprintf(stderr, "%s: Memory allocation failed\n", __func__);
        free_argv(argv, argc);
        free_argv(argv_rb, argc_rb);
        return EXIT_FAILURE;
    }
    dup_argv(&((cmds)[*cmd_idx]->argv), argv, argc);
    dup_argv(&((cmds)[*cmd_idx]->rollback_argv), argv_rb, argc_rb);
    (cmds)[*cmd_idx]->argc = argc;
    (cmds)[*cmd_idx]->rollback_argc = argc_rb;
    (*cmd_idx)++;
    free_argv(argv, argc);
    free_argv(argv_rb, argc_rb);
    return EXIT_SUCCESS;
}

/**
 * create argument name from dnoe
 * @param [in] dnode lyd_node
 * @param [in] startcmd_op_val starnode op_val
 * @param [out] value the captured arg name
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int create_cmd_arg_name(struct lyd_node *dnode, oper_t startcmd_op_val, char **arg_name)
{
    // if operation delete generate args only if:
    // - node is key  or startcmd has INCLUDE_ALL_ON_DELETE extention.
    int is_include_all_on_delete = 0;
    struct lyd_node *startcmd = get_parent_startcmd(dnode);
    if (startcmd) {
        if (get_extension(INCLUDE_ALL_ON_DELETE, startcmd, NULL) == EXIT_SUCCESS)
            is_include_all_on_delete = 1;
    }
    if (startcmd_op_val == DELETE_OPR && !lysc_is_key(dnode->schema) && !is_include_all_on_delete)
        return EXIT_SUCCESS;

    // check if this is leaf delete
    oper_t leaf_op_val = get_operation(dnode);
    if (leaf_op_val == DELETE_OPR) {
        char *on_node_delete = NULL;
        if (get_extension(ON_NODE_DELETE_EXT, dnode, &on_node_delete) == EXIT_SUCCESS) {
            if (on_node_delete == NULL) {
                fprintf(stderr,
                        "%s: ipr2cgen:on-leaf-delete extension found but failed to "
                        "get its arg value form node \"%s\"\n",
                        __func__, dnode->schema->name);
                return EXIT_FAILURE;
            }
            *arg_name = on_node_delete;
            return EXIT_SUCCESS;
        } else if (!is_include_all_on_delete)
            return EXIT_SUCCESS;
    }

    if (get_extension(FLAG_EXT, dnode, NULL) == EXIT_SUCCESS) {
        if (!strcmp("true", lyd_get_value(dnode)))
            *arg_name = strdup(dnode->schema->name);
        return EXIT_SUCCESS;
    }

    if (get_extension(VALUE_ONLY_EXT, dnode, NULL) == EXIT_SUCCESS)
        return EXIT_SUCCESS;
    if (get_extension(ARG_NAME_EXT, dnode, arg_name) == EXIT_SUCCESS) {
        if (*arg_name == NULL) {
            fprintf(stderr,
                    "%s: ipr2cgen:arg-name extension found but failed to "
                    "get the arg-name value for node \"%s\"\n",
                    __func__, dnode->schema->name);
            return EXIT_FAILURE;
        }
    } else
        *arg_name = strdup(dnode->schema->name);
    return EXIT_SUCCESS;
}

/**
 * create argument value from dnoe
 * @param [in] dnode lyd_node
 * @param [in] curr_op_val starnode op_val
 * @param [out] value the captured arg value value
 */
void create_cmd_arg_value(struct lyd_node *dnode, oper_t startcmd_op_val, char **arg_value)
{
    // if operation delete generate args only if:
    // - node is key  or startcmd has INCLUDE_ALL_ON_DELETE extention.
    int is_include_all_on_delete = 0;
    struct lyd_node *startcmd = get_parent_startcmd(dnode);
    if (startcmd) {
        if (get_extension(INCLUDE_ALL_ON_DELETE, startcmd, NULL) == EXIT_SUCCESS)
            is_include_all_on_delete = 1;
    }

    if (startcmd_op_val == DELETE_OPR && !lysc_is_key(dnode->schema) && !is_include_all_on_delete)
        return;

    // check if this is leaf delete
    oper_t leaf_op_val = get_operation(dnode);
    if (leaf_op_val == DELETE_OPR && !is_include_all_on_delete)
        return;

    // if FLAG extension, add schema name to the cmd and go to next iter.
    if (get_extension(FLAG_EXT, dnode, NULL) == EXIT_SUCCESS) {
        return;
    }
    if (arg_value == NULL)
        arg_value = malloc(sizeof(char *));
    // if list and grouping group the values.
    if (dnode->schema->nodetype == LYS_LIST) {
        char *group_list_separator = NULL;
        char *group_leafs_values_separator = NULL;
        if ((get_extension(GROUP_LIST_WITH_SEPARATOR_EXT, dnode, &group_list_separator) ==
             EXIT_SUCCESS) &&
            (get_extension(GROUP_LEAFS_VALUES_SEPARATOR_EXT, dnode,
                           &group_leafs_values_separator) == EXIT_SUCCESS)) {
            // add space
            char temp_value[50] = { 0 };

            strlcat(temp_value, " ", sizeof(temp_value));
            struct lyd_node *list_first_entry = lyd_first_sibling(dnode);
            struct lyd_node *list_next;

            // loop through the list.
            LY_LIST_FOR(list_first_entry, list_next)
            {
                struct lyd_node *list_first_leaf = lyd_child(list_next);
                struct lyd_node *leaf_next = NULL;
                // loop through the list entries and add them to cmd_line with the specified separator
                LY_LIST_FOR(list_first_leaf, leaf_next)
                {
                    strlcat(temp_value, lyd_get_value(leaf_next), sizeof(temp_value));
                    if (leaf_next->next != NULL)
                        strlcat(temp_value, group_leafs_values_separator, sizeof(temp_value));
                }
                // add the list separator.
                if (list_next->next != NULL &&
                    !strcmp(list_next->next->schema->name, list_next->schema->name))
                    strlcat(temp_value, group_list_separator, sizeof(temp_value));
            }
            *arg_value = strdup(temp_value);
        }
    } else {
        // if INDENT type remove the module name from the value (example: ip-link-type:dummy)
        // this strip ip-link-type.
        LY_DATA_TYPE type = ((struct lysc_node_leaf *)dnode->schema)->type->basetype;
        if (type == LY_TYPE_IDENT)
            *arg_value = strip_yang_iden_prefix(lyd_get_value(dnode));
        else
            *arg_value = (char *)strdup(lyd_get_value(dnode));
        char *add_static_arg;
        if (get_extension(AFTER_NODE_ADD_STATIC_ARG_EXT, dnode, &add_static_arg) == EXIT_SUCCESS) {
            char *static_arg = NULL, *xpath_arg = NULL;
            char fin_arg_value[1024] = { 0 };

            extract_static_and_xpath_args(add_static_arg, &static_arg, &xpath_arg);
            strlcat(fin_arg_value, *arg_value, sizeof(fin_arg_value));
            strlcat(fin_arg_value, " ", sizeof(fin_arg_value));
            strlcat(fin_arg_value, static_arg, sizeof(fin_arg_value));

            if (xpath_arg != NULL) {
                struct ly_set *match_set = NULL;
                int ret = lyd_find_xpath(dnode, xpath_arg, &match_set);
                //                lyd_find_xpath(dnode, "../../name", &match_set);
                if (match_set != NULL) {
                    free(xpath_arg);
                    xpath_arg = strdup(lyd_get_value(match_set->dnodes[0]));
                    strlcat(fin_arg_value, xpath_arg, sizeof(fin_arg_value));
                    //                    ly_set_free(match_set, NULL);
                } else {
                    fprintf(
                        stderr,
                        "%s: failed to get xpath_arg found in AFTER_NODE_ADD_STATIC_ARG extension."
                        " for node = \"%s\" : %s\n",
                        __func__, dnode->schema->name, sr_strerror(ret));
                    // TODO: fine an exit way. the whole change should fail.
                }
            }
            free(xpath_arg);
            free(add_static_arg);
            free(static_arg);
            free(*arg_value);
            *arg_value = strdup(fin_arg_value);
        }
    }
}

/**
 * get the target node from sysrepo running ds,
 * @param  [in] startcmd_node start cmd_node,
 * @param  [in] node_name node name to be fetched from sr.
 * @return [out] lyd_node found.
 */
struct lyd_node *get_node_from_sr(const struct lyd_node *startcmd_node, char *node_name)
{
    char xpath[1024] = { 0 };
    int ret;
    lyd_path(startcmd_node, LYD_PATH_STD, xpath, 1024);
    if (node_name) {
        strlcat(xpath, "/", sizeof(xpath));
        strlcat(xpath, node_name, sizeof(xpath));
    }
    sr_data_t *sr_data;

    // get the dnode from sr
    if (node_name)
        ret = sr_get_node(sr_session, xpath, 0, &sr_data);
    else
        ret = sr_get_data(sr_session, xpath, 0, 0, 0, &sr_data);
    //        ret = sr_get_subtree(sr_session, xpath, 0, &sr_data);
    if (ret != SR_ERR_OK) {
        fprintf(stderr,
                "%s: failed to get node data from sysrepo ds."
                " xpath = \"%s\": %s\n",
                __func__, xpath, sr_strerror(ret));
        return NULL;
    }
    return sr_data->tree;
}

/**
 * create iproute2 arguments out of lyd2 node, this will take the op_val into consideration,
 * @param startcmd_node lyd_node to generate arg for, example link, nexthop, filter ... etc
 * @param op_val operation value
 * @return
 */
char *lyd2cmdline_args(const struct lyd_node *startcmd_node, oper_t op_val)
{
    char cmd_line[CMD_LINE_SIZE] = { 0 };
    char tail_arg[64] = { 0 };

    int ret;
    struct lyd_node *next;
    LYD_TREE_DFS_BEGIN(lyd_child(startcmd_node), next) // skip the starcmd node itself
    {
        char *on_update_include = NULL, *add_static_arg = NULL;
        char *group_list_separator = NULL;
        char *group_leafs_values_separator = NULL;
        char *arg_name = NULL, *arg_value = NULL;
start:
        switch (next->schema->nodetype) {
        case LYS_LIST:
            // if the list node is startcmd or it's parent has del operation then skip it.
            if (is_startcmd_node(next) || op_val == DELETE_OPR) {
                // if the parent startcmd is delete, we don't want to execute this cmd, just skip.
                if (next->next) { // move to next sibling and start from beginning,
                    next = next->next;
                    goto start;
                } else
                    goto done; // no more sibling, go to done.
            }

        case LYS_CONTAINER:
            // if this and empty "when all container" skip.
            // if when condition added, the container will be included in the lyd tree even if
            // no data inside the container, container will have LYD_DEFAULT|LYD_WHEN_TRUE flags
            if ((next->flags & LYD_DEFAULT) && (next->flags & LYD_WHEN_TRUE))
                break;
            if (op_val == DELETE_OPR)
                break;
            if (get_extension(ADD_STATIC_ARG_EXT, next, &add_static_arg) == EXIT_SUCCESS) {
                if (add_static_arg == NULL) {
                    fprintf(stderr,
                            "%s: ADD_STATIC_ARG extension found,"
                            "but failed to retrieve the the static arg extension value "
                            "for node \"%s\" \n",
                            __func__, next->schema->name);
                    return NULL;
                }
                // check if the container has child nodes, when container has extention, libyang
                // create an empty node container. if the container has children then add the
                // static arg
                if (lyd_child(next)) {
                    strlcat(cmd_line, " ", sizeof(cmd_line));
                    strlcat(cmd_line, add_static_arg, sizeof(cmd_line));
                }
                free(add_static_arg);
            }
            if (op_val == UPDATE_OPR &&
                get_extension(ON_UPDATE_INCLUDE_EXT, next, &on_update_include) == EXIT_SUCCESS) {
                // capture the on-update-include ext,
                if (on_update_include == NULL) {
                    fprintf(stderr,
                            "%s: ON_UPDATE_INCLUDE extension found,"
                            "but failed to retrieve the arg-name list from ON_UPDATE_INCLUDE "
                            "extension for node \"%s\" \n",
                            __func__, next->schema->name);
                    return NULL;
                }
                // get all the on_update_include arg-names, fetch them from sr, and add them to the cmd_line.
                // args-names format = arg1, arg2, ... argn
                char *token;

                // Get the first arg
                token = strtok(on_update_include, ",");
                while (token != NULL) {
                    arg_name = NULL;
                    arg_value = NULL;

                    struct lyd_node *include_node = get_node_from_sr(startcmd_node, token);
                    if (include_node == NULL)
                        return NULL;

                    create_cmd_arg_value(include_node, op_val, &arg_value);
                    ret = create_cmd_arg_name(include_node, op_val, &arg_name);
                    if (ret != EXIT_SUCCESS) {
                        fprintf(stderr,
                                "%s: failed to create create_cmd_arg_name for node \"%s\".\n",
                                __func__, next->schema->name);
                        return NULL;
                    }

                    if (arg_name != NULL) {
                        strlcat(cmd_line, " ", sizeof(cmd_line));
                        strlcat(cmd_line, arg_name, sizeof(cmd_line));
                        free(arg_name);
                    }
                    if (arg_value != NULL) {
                        strlcat(cmd_line, " ", sizeof(cmd_line));
                        strlcat(cmd_line, arg_value, sizeof(cmd_line));
                        free(arg_value);
                    }
                    // get next token.
                    token = strtok(NULL, ",");
                }
                break;

            } else if ((get_extension(GROUP_LIST_WITH_SEPARATOR_EXT, next, &group_list_separator) ==
                        EXIT_SUCCESS) &&
                       (get_extension(GROUP_LEAFS_VALUES_SEPARATOR_EXT, next,
                                      &group_leafs_values_separator) == EXIT_SUCCESS)) {
                if (group_list_separator == NULL) {
                    fprintf(stderr,
                            "%s: failed to get group_list_separator"
                            " for node \"%s\".\n",
                            __func__, next->schema->name);
                    return NULL;
                }
                if (group_leafs_values_separator == NULL) {
                    fprintf(stderr,
                            "%s: failed to get group_leafs_values_separator"
                            " for node \"%s\".\n",
                            __func__, next->schema->name);
                    return NULL;
                }
                create_cmd_arg_value(next, op_val, &arg_value);
                ret = create_cmd_arg_name(next, op_val, &arg_name);
                if (ret != EXIT_SUCCESS) {
                    fprintf(stderr, "%s: failed to create create_cmd_arg_name for node \"%s\".\n",
                            __func__, next->schema->name);
                    return NULL;
                }
                if (arg_name != NULL) {
                    strlcat(cmd_line, " ", sizeof(cmd_line));
                    strlcat(cmd_line, arg_name, sizeof(cmd_line));
                    free(arg_name);
                }
                if (arg_value != NULL) {
                    strlcat(cmd_line, " ", sizeof(cmd_line));
                    strlcat(cmd_line, arg_value, sizeof(cmd_line));
                    free(arg_value);
                }
                const char *grouped_schema_name = next->schema->name;
                // skip the collected list info. while the next is not null and the next node is
                // same node schema name, then move next.
                while (next->next != NULL) {
                    if (!strcmp(next->schema->name, grouped_schema_name))
                        next = next->next;
                    else
                        break;
                }
            }
            // next might be skipped,
            // if after skip, the node type of next is leaf or leaflist then continue to
            // leaf and leaflist cases to capture the arg_name arg_value for that node.
            if (next->schema->nodetype != LYS_LEAF && next->schema->nodetype != LYS_LEAFLIST)
                break;
        case LYS_LEAF:
        case LYS_LEAFLIST:
            arg_name = NULL;
            arg_value = NULL;
            create_cmd_arg_value(next, op_val, &arg_value);
            ret = create_cmd_arg_name(next, op_val, &arg_name);
            if (ret != EXIT_SUCCESS) {
                fprintf(stderr, "%s: failed to create create_cmd_arg_name for node \"%s\".\n",
                        __func__, next->schema->name);
                return NULL;
            }
            int is_tail_arg = 0;
            if (get_extension(ADD_LEAF_AT_END, next, NULL) == EXIT_SUCCESS)
                is_tail_arg = 1;
            if (arg_name != NULL) {
                if (is_tail_arg) {
                    strlcat(tail_arg, " ", sizeof(tail_arg));
                    strlcat(tail_arg, arg_name, sizeof(tail_arg));
                } else {
                    strlcat(cmd_line, " ", sizeof(cmd_line));
                    strlcat(cmd_line, arg_name, sizeof(cmd_line));
                }
                free(arg_name);
            }
            if (arg_value != NULL) {
                if (is_tail_arg) {
                    strlcat(tail_arg, " ", sizeof(tail_arg));
                    strlcat(tail_arg, arg_value, sizeof(tail_arg));
                } else {
                    strlcat(cmd_line, " ", sizeof(cmd_line));
                    strlcat(cmd_line, arg_value, sizeof(cmd_line));
                }
                free(arg_value);
            }
            break;
        }
        LYD_TREE_DFS_END(startcmd_node, next)
    }
done:
    strlcat(cmd_line, tail_arg, sizeof(cmd_line)); // add the tail arg to the line.
    return strdup(cmd_line);
}

/**
 * @brief this will fetch the startcmd_node from sysrepo, and merge it with the change node
 * this will result on a replace on update change node.
 * @param dnode the change lyd_node.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */

int ext_onupdate_replace_hdlr(struct lyd_node **dnode)
{
    int ret;
    // get the original node from sysrepo
    struct lyd_node *original_dnode = get_node_from_sr(*dnode, NULL);
    if (!original_dnode) {
        fprintf(stderr, "%s: failed to get original_dnode from sysrepo ds. \"%s\" \n", __func__,
                (*dnode)->schema->name);
        return EXIT_FAILURE;
    }

    struct lyd_node *dnode_top_level = lyd_parent(*dnode);

    struct lyd_node *dnext;
    LYD_TREE_DFS_BEGIN(original_dnode, dnext)
    {
        char dxpath[1024] = { 0 };
        lyd_path(dnext, LYD_PATH_STD, dxpath, 1024);
        // check if the original dnode exist in the change node, if exist skip. we
        // want to add the none existing node only.
        ret = lyd_find_path(dnode_top_level, dxpath, 0, NULL);
        if (ret != LY_SUCCESS) {
            struct lyd_node *new_dnode;
            lyd_new_path(dnode_top_level, NULL, dxpath, lyd_get_value(dnext), 0, &new_dnode);
            ret = lyd_new_meta(NULL, new_dnode, NULL, "yang:operation", "create", 0, NULL);
            if (ret != LY_SUCCESS) {
                fprintf(stderr,
                        "%s: failed to set meta data 'yang:operation=create' for node. \"%s\" \n",
                        __func__, (*dnode)->schema->name);
                lyd_free_all(original_dnode);
            }
        }
        LYD_TREE_DFS_END(original_dnode, dnext)
    }
    lyd_free_all(original_dnode);
    return EXIT_SUCCESS;
}

char *lyd2cmd_line(struct lyd_node *startcmd_node, char *oper2cmd_prefix[3])
{
    oper_t op_val;
    char cmd_line[CMD_LINE_SIZE] = { 0 };

    // prepare for new command
    op_val = get_operation(startcmd_node);

    // special case: for inner start_cmd, where the operation need to be taken from parent node.
    if (op_val == UNKNOWN_OPR) {
        struct lyd_node *parent_with_oper_val = lyd_parent(startcmd_node);
        oper_t parent_op_val = UNKNOWN_OPR;
        // find first parent node with oper_val
        while (parent_with_oper_val) {
            parent_op_val = get_operation(parent_with_oper_val);
            if (parent_op_val != UNKNOWN_OPR)
                break;
            parent_with_oper_val = lyd_parent(parent_with_oper_val);
        }
        //        if (parent_with_oper_val) {
        int ret;
        switch (parent_op_val) {
        case ADD_OPR:
            ret = lyd_new_meta(NULL, startcmd_node, NULL, "yang:operation", "create", 0, NULL);
            op_val = ADD_OPR;
            break;
        case DELETE_OPR:
            ret = lyd_new_meta(NULL, startcmd_node, NULL, "yang:operation", "delete", 0, NULL);
            op_val = DELETE_OPR;
            break;
        case UPDATE_OPR: // if parent is update, we can't confirm if the child is add,delete or update.
        case UNKNOWN_OPR:
            fprintf(stderr, "%s: unknown or update operation for parent startcmd node = \"%s\"\n",
                    __func__, startcmd_node->schema->name);
            return NULL;
        }
        if (ret != LY_SUCCESS) {
            fprintf(stderr,
                    "%s: failed to set meta data operation for inner startcmd for node. \"%s\" \n",
                    __func__, startcmd_node->schema->name);
            return NULL;
        }

        //        } else {
        //            fprintf(stderr, "%s: unknown operation for startcmd node \"%s\" \n", __func__,
        //                    startcmd_node->schema->name);
        //            return NULL;
        //        }
    }

    if (op_val == UPDATE_OPR &&
        (get_extension(REPLACE_ON_UPDATE_EXT, startcmd_node, NULL) == EXIT_SUCCESS)) {
        ext_onupdate_replace_hdlr(&startcmd_node);
    }
    // add cmd prefix to the cmd_line
    strlcpy(cmd_line, oper2cmd_prefix[op_val], sizeof(cmd_line));
    // check if this starcmd is including the parent leafs (tc filter case)
    if (get_extension(INCLUDE_PARENT_LEAFS, startcmd_node, NULL) == EXIT_SUCCESS) {
        struct lyd_node *start_cmd_parent = lyd_parent(startcmd_node);
        char *parent_cmd_args = lyd2cmdline_args(start_cmd_parent, op_val);
        strlcat(cmd_line, parent_cmd_args, sizeof(cmd_line));
        free(parent_cmd_args);
    }
    // get the cmd args for the startcmd_node
    char *cmd_args = lyd2cmdline_args(startcmd_node, op_val);
    if (cmd_args == NULL) {
        fprintf(stderr, "%s: failed to create cmdline arguments for node \"%s\" \n", __func__,
                startcmd_node->schema->name);
        return NULL;
    }
    strlcat(cmd_line, cmd_args, sizeof(cmd_line));
    free(cmd_args);
    return strdup(cmd_line);
}

int find_matching_target_lrefs(const struct lyd_node *all_change_nodes, struct lyd_node *startcmd,
                               struct lysc_type_leafref *lref_t, struct ly_set **found_leafrefs_set)
{
    char xpath[1024] = { 0 };
    struct ly_set *target_set, *s_set = NULL;
    struct lysc_node *target_startcmd_y_node = NULL;
    int ret;
    // get the schema node of the leafref.
    ret = lys_find_expr_atoms(startcmd->schema, startcmd->schema->module, lref_t->path,
                              lref_t->prefixes, 0, &s_set);
    if (s_set == NULL || ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to get target leafref for node \"%s\": %s\n", __func__,
                startcmd->schema->name, ly_strerrcode(ret));
        return EXIT_FAILURE;
    }

    target_startcmd_y_node = s_set->snodes[s_set->count - 1];
    // get the xpath of the schema node.
    lysc_path(target_startcmd_y_node, LYSC_PATH_DATA, xpath, 1024);
    // get all target nodes.
    ret = lyd_find_xpath(all_change_nodes, xpath, &target_set);
    if (ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to found target startcmd nodes for xpath `%s`: %s.\n", __func__,
                xpath, ly_strerrcode(ret));
        return EXIT_FAILURE;
    }
    // loop through all found target dnodes and get the matching one to the current
    // next dnode.
    for (uint32_t i = 0; i < target_set->count; i++) {
        if (!strcmp(lyd_get_value(startcmd), lyd_get_value(target_set->dnodes[i]))) {
            // get the parnet startcmd node for the matched target dnode.
            struct lyd_node *target_parent = lyd_parent(target_set->dnodes[i]);
            while (target_parent != NULL) {
                if (is_startcmd_node(target_parent))
                    break;
                target_parent = lyd_parent(target_parent);
            }
            if (target_parent == NULL) {
                fprintf(stderr, "%s: no matching startcmd node found, target_node`%s`.\n", __func__,
                        target_set->dnodes[i]->schema->name);
                return EXIT_FAILURE;
            }
            // add the target startcmd to the found_leafrefs_set.
            ret = ly_set_add(*found_leafrefs_set, target_parent, 0, NULL);
            if (ret != LY_SUCCESS) {
                fprintf(stderr,
                        "%s: failed to add target startcmd to found_leafrefs_set `%s`: %s.\n",
                        __func__, target_set->dnodes[i]->schema->name, ly_strerrcode(ret));
                return EXIT_FAILURE;
            }
        }
    }
    return EXIT_SUCCESS;
}

/**
 * @brief get all leafrefs of startcmd node, and add them to found_leafrefs
 * @param all_change_nodes [in]  all change nodes
 * @param startcmd_node     [in]  lyd_node to create cmd_info for and add it to cmds.
 * @param found_leafrefs   [out] all leafrefs founded, the leafrefs will be duplicates of the
 *                               original leafref, and their parents will linked together.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int get_node_leafrefs(const struct lyd_node *all_change_nodes, struct lyd_node *startcmd,
                      struct ly_set **found_leafrefs_set)
{
    int ret;

    struct lyd_node *next = NULL;
    // check if this startcmd is also including parent node's leafs.
    if (get_extension(INCLUDE_PARENT_LEAFS, startcmd, NULL) == EXIT_SUCCESS)
        get_node_leafrefs(all_change_nodes, lyd_parent(startcmd), found_leafrefs_set);

    LYD_TREE_DFS_BEGIN(startcmd, next)
    {
        if (next->schema->nodetype == LYS_LEAF) {
            LY_DATA_TYPE type = ((struct lysc_node_leaf *)next->schema)->type->basetype;
            // union might have leafrefs.
            if (type == LY_TYPE_UNION) {
                struct lysc_type_union *y_union_t = NULL;
                y_union_t =
                    (struct lysc_type_union *)(((struct lysc_node_leaf *)next->schema)->type);
                LY_ARRAY_COUNT_TYPE i_sized;
                // loop through all types and check which one
                LY_ARRAY_FOR(y_union_t->types, i_sized)
                {
                    // check if the type is leafref
                    if (y_union_t->types[i_sized]->basetype != LY_TYPE_LEAFREF)
                        continue;
                    struct lysc_type_leafref *lref_t =
                        (struct lysc_type_leafref *)y_union_t->types[i_sized];
                    ret = find_matching_target_lrefs(all_change_nodes, next, lref_t,
                                                     found_leafrefs_set);
                    if (ret != EXIT_SUCCESS) {
                        fprintf(stderr,
                                "%s: fail to find and add matching target for node `%s`: %s.\n",
                                __func__, next->schema->name, ly_strerrcode(ret));
                        return EXIT_FAILURE;
                    }
                }
            } else if (type == LY_TYPE_LEAFREF) {
                // get the schema node of the leafref.
                struct lysc_type_leafref *lref_t =
                    (struct lysc_type_leafref *)(((struct lysc_node_leaf *)next->schema)->type);
                ret =
                    find_matching_target_lrefs(all_change_nodes, next, lref_t, found_leafrefs_set);
                if (ret != EXIT_SUCCESS) {
                    fprintf(stderr, "%s: fail to find and add matching target for node `%s`: %s.\n",
                            __func__, next->schema->name, ly_strerrcode(ret));
                    return EXIT_FAILURE;
                }
            }
        }
        LYD_TREE_DFS_END(startcmd, next)
    }
    return EXIT_SUCCESS;
}

/**
 * @brief create cmd_info for startcmd_node and add it to cmds.
 * @param cmds [in,out] array of cmd_info
 * @param cmd_idx [in,out] current index pointer for cmds
 * @param startcmd_node [in] lyd_node to create cmd_info for and add it to cmds.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int add_cmd_info_core(struct cmd_info **cmds, int *cmd_idx, struct lyd_node *startcmd_node)
{
    // if the parent is startcmd, and the parent is delete, skip this inner startcmd.
    struct lyd_node *startcmd_parent = lyd_parent(startcmd_node);
    if (is_startcmd_node(startcmd_parent) && get_operation(startcmd_parent) == DELETE_OPR)
        return EXIT_SUCCESS;

    int ret = EXIT_SUCCESS;
    char *oper2cmd_prefix[3] = { NULL };
    char *cmd_line = NULL, *rollback_cmd_line = NULL;
    struct lyd_node *rollback_dnode = NULL;
    // first get the add, update, delete cmds prefixis from schema extensions
    if (get_extension(CMD_ADD_EXT, startcmd_node, &oper2cmd_prefix[ADD_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr,
                "%s: cmd-add extension is missing from root container "
                "make sure root container has ipr2cgen:cmd-add\n",
                __func__);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    if (get_extension(CMD_DELETE_EXT, startcmd_node, &oper2cmd_prefix[DELETE_OPR]) !=
        EXIT_SUCCESS) {
        fprintf(stderr,
                "%s: cmd-delete extension is missing from root container "
                "make sure root container has ipr2cgen:cmd-delete\n",
                __func__);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    if (get_extension(CMD_UPDATE_EXT, startcmd_node, &oper2cmd_prefix[UPDATE_OPR]) !=
        EXIT_SUCCESS) {
        fprintf(stderr,
                "%s: ipr2cgen:cmd-update extension is missing from root container "
                "make sure root container has ipr2cgen:cmd-update\n",
                __func__);
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    cmd_line = lyd2cmd_line(startcmd_node, oper2cmd_prefix);
    if (cmd_line == NULL) {
        fprintf(stderr, "%s: failed to generate ipr2 cmd for node \"%s\" \n", __func__,
                startcmd_node->schema->name);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    // before calling diff_reserve we need to do dup_single, otherwise all sibling startcmds,
    // will be reversed
    struct lyd_node *tmp_startcmd;
    lyd_dup_single(startcmd_node, NULL, LYD_DUP_RECURSIVE, &tmp_startcmd);
    // get the rollback node.
    ret = lyd_diff_reverse_all(tmp_startcmd, &rollback_dnode);
    lyd_free_all(tmp_startcmd);
    if (ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to create rollback_dnode by lyd_diff_reverse_all(): %s\n",
                __func__, ly_strerrcode(ret));
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    // the reversed node come with no parent, so we need to duplicate startcmd parent, and insert
    // the rollback_node into the duplicated parent.
    // this is needed when fetching data from sr, otherwise the fetch will fail.
    struct lyd_node *rollback_dnode_parent = NULL;
    lyd_dup_single(lyd_parent(startcmd_node), NULL, LYD_DUP_WITH_PARENTS | LYD_DUP_WITH_FLAGS,
                   &rollback_dnode_parent);
    ret = lyd_insert_child(rollback_dnode_parent, rollback_dnode);
    if (ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to insert rollback node to its parent node: %s\n", __func__,
                ly_strerrcode(ret));
        lyd_free_all(rollback_dnode_parent);
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    rollback_cmd_line = lyd2cmd_line(rollback_dnode, oper2cmd_prefix);
    if (rollback_cmd_line == NULL) {
        fprintf(stderr, "%s: failed to generate ipr2 rollback cmd for node \"%s\" \n", __func__,
                rollback_dnode->schema->name);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    ret = add_command(cmds, cmd_idx, cmd_line, rollback_cmd_line);

cleanup:
    if (cmd_line)
        free(cmd_line);
    if (rollback_cmd_line)
        free(rollback_cmd_line);
    if (rollback_dnode)
        lyd_free_all(rollback_dnode);
    if (oper2cmd_prefix[ADD_OPR])
        free(oper2cmd_prefix[ADD_OPR]);
    if (oper2cmd_prefix[UPDATE_OPR])
        free(oper2cmd_prefix[UPDATE_OPR]);
    if (oper2cmd_prefix[DELETE_OPR])
        free(oper2cmd_prefix[DELETE_OPR]);
    return ret;
}

/**
 * this function will ensure that the lyd_node first is added before the lyd_node second,
 * weather  the lyd_nodes are already added to the set or not.
 * @param ordered_set [in,out] the ly_set to insert the nodes in.
 * @param first [in] the first lyd_node to be inserted.
 * @param second [in] the second lyd_node to be inserted.
 */
void ly_set_insert_before(struct ly_set **ordered_set, struct lyd_node *first,
                          struct lyd_node *second)
{
    uint32_t i_first, i_second;
    struct ly_set *temp_set;
    ly_set_new(&temp_set);
    // check if first is exist in the set
    if (ly_set_contains(*ordered_set, second, &i_second)) {
        if (ly_set_contains(*ordered_set, first, &i_first)) {
            // first already added before second.
            if (i_first < i_second)
                return;
        }
        // we need to swap.
        // [1] move the second node and all nodes after it to the temp set.
        for (uint32_t i = i_second; i < (*ordered_set)->count; i++) {
            ly_set_add(temp_set, (*ordered_set)->dnodes[i], 0, NULL);
            // this remove will keep moving the nodes after the index, so we keep deleting the i_second
        }
        while (i_second < (*ordered_set)->count) {
            // till the end of the list.
            ly_set_rm_index_ordered(*ordered_set, i_second, NULL);
        }
        // [2] add the first node
        ly_set_add(*ordered_set, first, 0, NULL);
        // [3] add the copied nodes back to the set
        for (uint32_t i = 0; i < temp_set->count; i++) {
            ly_set_add(*ordered_set, temp_set->dnodes[i], 0, NULL);
        }
        ly_set_add(*ordered_set, second, 0, NULL);
        return;
    }
    // second does not exist then,  add first and then second. NOTE: set handle duplication.
    ly_set_add(*ordered_set, first, 0, NULL);
    ly_set_add(*ordered_set, second, 0, NULL);
}

/**
 * add all dependencies for node startcmd_node, this add dependencies recursively.
 * @param all_change_nodes [in] all change nodes
 * @param startcmd_node [in] startcmd_node to find and add its dependencies.
 * @param sorted_dependecies [in,out] the ly_set to be sorted.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int add_node_dependencies(const struct lyd_node *all_change_nodes, struct lyd_node *startcmd_node,
                          struct ly_set *sorted_dependecies)
{
    int ret = EXIT_SUCCESS;
    if (startcmd_node->priv == &processed)
        return ret;

    struct ly_set *node_leafrefs = NULL;
    ly_set_new(&node_leafrefs);

    // get the node leafrefs
    ret = get_node_leafrefs(all_change_nodes, startcmd_node, &node_leafrefs);
    if (ret != EXIT_SUCCESS) {
        fprintf(stderr, "%s: failed to get the leafref depenedecnies for node \"%s\"\n ", __func__,
                startcmd_node->schema->name);
        return EXIT_FAILURE;
    }

    // check if dependencies found.
    if (node_leafrefs != NULL) {
        // - if the start_cmd node's operation is delete, then add it before it's leafrefs.
        // - if the start_cmd node's operation is add, then add leafref before
        if (get_operation(startcmd_node) == DELETE_OPR ||
            get_operation(lyd_parent(startcmd_node)) == DELETE_OPR) {
            // check if the startcmd's leafrefs already exist in sorted_dependecies,
            // if it exist then insert the startcmd before the lowest lref index in sorted_dependecies.
            // [1] process all the depenencies recurcivly
            for (uint32_t i = 0; i < node_leafrefs->count; i++) {
                struct lyd_node *leafref_node = node_leafrefs->dnodes[i];
                add_node_dependencies(all_change_nodes, leafref_node, sorted_dependecies);
            }
            // [2] find the lref with lowest index in sorted_dependecies.
            uint32_t lowest_lref_index = sorted_dependecies->count;
            struct lyd_node *lowest_lref_node = NULL;
            for (uint32_t i = 0; i < node_leafrefs->count; i++) {
                struct lyd_node *leafref_node = node_leafrefs->dnodes[i];
                uint32_t lref_i = 0;
                if (ly_set_contains(sorted_dependecies, leafref_node, &lref_i)) {
                    if (lowest_lref_index > lref_i) {
                        lowest_lref_index = lref_i;
                        lowest_lref_node = leafref_node;
                    }
                }
            }
            // [2] insert the startcmd before the lowest_lref_node in the sorted_dependecies
            if (lowest_lref_node) {
                ly_set_insert_before(&sorted_dependecies, startcmd_node, lowest_lref_node);
            }
        } else {
            // the operations is add, so we need to add all startcmd's lrefs before the startcmd.
            for (uint32_t i = 0; i < node_leafrefs->count; i++) {
                struct lyd_node *leafref_node = node_leafrefs->dnodes[i];
                add_node_dependencies(all_change_nodes, leafref_node, sorted_dependecies);
            }
        }
    }

    // finally, add the current startcmd_node to the sorted set. if you try to add an existing node,
    // no problem.
    ly_set_add(sorted_dependecies, startcmd_node, 0, NULL);
    startcmd_node->priv = &processed;

    return ret;
}

/**
 * sort lyd_nodes dependencies based on the startcmd_node leafrefs.
 * if the startcmd_node is delete, it's added before its leafrefs, if it's add, it's added after
 * its leafrefs.
 * @param start_cmds_set [in] ly_set of unsorted startcmds.
 * @param all_change_nodes  [in] lyd_node for all changed nodes in this trax. need for find/search
 * @param sorted_startcmds [out] sorted startcmds.
 * @return EXIT_SUCCESS
 * @return EXIT_FAILURE
 */
int sort_lyd_dependencies(struct ly_set *start_cmds_set, const struct lyd_node *all_change_nodes,
                          struct ly_set *sorted_startcmds)
{
    for (int i = 0; i < start_cmds_set->count; i++) {
        int ret =
            add_node_dependencies(all_change_nodes, start_cmds_set->dnodes[i], sorted_startcmds);

        if (ret != EXIT_SUCCESS) {
            return ret;
        }
    }
    return EXIT_SUCCESS;
}

struct cmd_info **lyd2cmds(const struct lyd_node *all_change_nodes)
{
    char *node_print_text;
    int cmd_idx = 0;
    struct cmd_info **cmds = malloc(CMDS_ARRAY_SIZE * sizeof(struct cmd_info *));
    if (cmds == NULL) {
        fprintf(stderr, "%s: Memory allocation failed\n", __func__);
        return NULL;
    }

    // Set all elements of the array to NULL
    for (int i = 0; i < CMDS_ARRAY_SIZE; i++) {
        cmds[i] = NULL;
    }

    const struct lyd_node *change_node;
    struct lyd_node *next = NULL;
    struct ly_set *start_cmds_set;
    ly_set_new(&start_cmds_set);
    // initialize ->priv to NULL.
    LY_LIST_FOR(all_change_nodes, change_node)
    {
        LYD_TREE_DFS_BEGIN(change_node, next)
        {
            if (is_startcmd_node(next)) {
                // if this startcmd is inner and its parent start cmd is delete,
                // then we don't want to generate cmd for it. as it will create error since
                // the parent item is deleted from iproute2 already.
                struct lyd_node *parent_scmd = lyd_parent(next);
                while (parent_scmd) {
                    if (is_startcmd_node(parent_scmd)) {
                        if (get_operation(parent_scmd) == DELETE_OPR)
                            goto next_iter;
                    }
                    parent_scmd = lyd_parent(parent_scmd);
                }
                ly_set_add(start_cmds_set, next, 0, 0);
            }
next_iter:
            LYD_TREE_DFS_END(change_node, next)
        }
        lyd_print_mem(&node_print_text, change_node, LYD_XML, 0);
        fprintf(stdout, "(+) change request received:\n%s", node_print_text);
        free(node_print_text);
    }

    // initialize the ->priv
    for (int i = 0; i < start_cmds_set->count; i++) {
        start_cmds_set->dnodes[i]->priv = NULL;
    }

    // first sort the dependencies.
    struct ly_set *sorted_startcmds;
    ly_set_new(&sorted_startcmds);
    sort_lyd_dependencies(start_cmds_set, all_change_nodes, sorted_startcmds);

    // generated command for the sorted dependencies
    for (int i = 0; i < sorted_startcmds->count; i++) {
        if (add_cmd_info_core(cmds, &cmd_idx, sorted_startcmds->dnodes[i]) != EXIT_SUCCESS) {
            free_cmds_info(cmds);
            return NULL;
        }
    }

    return cmds;
}
