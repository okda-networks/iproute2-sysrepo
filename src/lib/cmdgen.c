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
    GROUP_LIST_WITH_SEPARATOR,
    GROUP_LEAFS_VALUES_SEPARATOR,

    // root container extensions
    CMD_ADD_EXT,
    CMD_DELETE_EXT,
    CMD_UPDATE_EXT,

    // leaf extensions
    ARG_NAME_EXT,
    FLAG_EXT,
    VALUE_ONLY_EXT,
    VALUE_ONLY_ON_UPDATE_EXT,

    AFTER_NODE_ADD_STATIC_ARG,
    ON_NODE_DELETE,

    //other
    ON_UPDATE_INCLUDE,
    ADD_STATIC_ARG

} extension_t;

char *yang_ext_map[] = { [CMD_START_EXT] = "cmd-start",
                         [CMD_ADD_EXT] = "cmd-add",
                         [CMD_DELETE_EXT] = "cmd-delete",
                         [CMD_UPDATE_EXT] = "cmd-update",
                         [GROUP_LIST_WITH_SEPARATOR] = "group-list-with-separator",
                         [GROUP_LEAFS_VALUES_SEPARATOR] = "group-leafs-values-separator",

                         // leaf extensions
                         [ARG_NAME_EXT] = "arg-name",
                         [FLAG_EXT] = "flag",
                         [VALUE_ONLY_EXT] = "value-only",
                         [VALUE_ONLY_ON_UPDATE_EXT] = "value-only-on-update",
                         [AFTER_NODE_ADD_STATIC_ARG] = "after-node-add-static-arg",
                         [ON_NODE_DELETE] = "on-node-delete",

                         // other
                         [ON_UPDATE_INCLUDE] = "on-update-include",
                         [ADD_STATIC_ARG] = "add-static-arg" };

void dup_argv(char ***dest, char **src, int argc)
{
    *dest = (char **)malloc(argc * sizeof(char *));
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
 * check if this node is cmd-start node.
 * example in yang the list nexthop is where the cmd-start ext added.
 * @param [in] dnode lyd_node
 * @return 1 if ext found.
 */
int is_startcmd_node(struct lyd_node *dnode)
{
    const struct lysc_node *schema = dnode->schema;
    LY_ARRAY_COUNT_TYPE i;
    LY_ARRAY_FOR(schema->exts, i)
    {
        if (schema->exts != NULL) {
            if (!strcmp(schema->exts[i].def->name, "cmd-start"))
                return 1;
        }
    }
    return 0;
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
    *argv = (char **)malloc((*argc) * sizeof(char *));
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
    // if list operation is delete, get the keys only
    if (startcmd_op_val == DELETE_OPR && !lysc_is_key(dnode->schema))
        return EXIT_SUCCESS;

    // check if this is leaf delete
    oper_t leaf_op_val = get_operation(dnode);
    if (leaf_op_val == DELETE_OPR) {
        char *on_node_delete = NULL;
        if (get_extension(ON_NODE_DELETE, dnode, &on_node_delete) == EXIT_SUCCESS) {
            if (on_node_delete == NULL) {
                fprintf(stderr,
                        "%s: ipr2cgen:on-leaf-delete extension found but failed to "
                        "get its arg value form node \"%s\"\n",
                        __func__, dnode->schema->name);
                return EXIT_FAILURE;
            }
            *arg_name = on_node_delete;
            return EXIT_SUCCESS;
        }
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
    // if list operation is delete, get the keys only
    if (startcmd_op_val == DELETE_OPR && !lysc_is_key(dnode->schema))
        return;

    // check if this is leaf delete
    oper_t leaf_op_val = get_operation(dnode);
    if (leaf_op_val == DELETE_OPR)
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
        if ((get_extension(GROUP_LIST_WITH_SEPARATOR, dnode, &group_list_separator) ==
             EXIT_SUCCESS) &&
            (get_extension(GROUP_LEAFS_VALUES_SEPARATOR, dnode, &group_leafs_values_separator) ==
             EXIT_SUCCESS)) {
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
        if (get_extension(AFTER_NODE_ADD_STATIC_ARG, dnode, &add_static_arg) == EXIT_SUCCESS) {
            size_t final_arg_value_len = strlen(*arg_value) + strlen(add_static_arg) + 2;
            char *final_arg_value = malloc(final_arg_value_len);
            memset(final_arg_value, '\0', final_arg_value_len);
            strlcat(final_arg_value, *arg_value, final_arg_value_len);
            strlcat(final_arg_value, " ", final_arg_value_len);
            strlcat(final_arg_value, add_static_arg, final_arg_value_len);
            free(*arg_value);
            *arg_value = final_arg_value;
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
    char xpath[512] = { 0 };
    int ret;
    lyd_path(startcmd_node, LYD_PATH_STD, xpath, 512);
    strlcat(xpath, "/", sizeof(xpath));
    strlcat(xpath, node_name, sizeof(xpath));
    sr_data_t *sr_data;
    // get the dnode from sr

    ret = sr_get_node(sr_session, xpath, 0, &sr_data);
    if (ret != SR_ERR_OK) {
        fprintf(stderr,
                "%s: failed to get include node data from sysrepo ds."
                " include node name = \"%s\" : %s\n",
                __func__, node_name, sr_strerror(ret));
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

    int ret;
    struct lyd_node *next;
    LYD_TREE_DFS_BEGIN(startcmd_node, next)
    {
        char *on_update_include = NULL, *add_static_arg = NULL;
        char *group_list_separator = NULL;
        char *group_leafs_values_separator = NULL;
        char *arg_name = NULL, *arg_value = NULL;
        switch (next->schema->nodetype) {
        case LYS_LIST:
        case LYS_CONTAINER:
            // if this and empty "when all container" skip.
            // if when condition added, the continer will be included in the lyd tree even if
            // no data inside the container, container will have LYD_DEFAULT|LYD_WHEN_TRUE flags
            if ((next->flags & LYD_DEFAULT) && (next->flags & LYD_WHEN_TRUE))
                break;
            if (get_extension(ADD_STATIC_ARG, next, &add_static_arg) == EXIT_SUCCESS) {
                if (add_static_arg == NULL) {
                    fprintf(stderr,
                            "%s: ADD_STATIC_ARG extension found,"
                            "but failed to retrieve the the static arg extension value "
                            "for node \"%s\" \n",
                            __func__, next->schema->name);
                    return NULL;
                }
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, add_static_arg, sizeof(cmd_line));
                free(on_update_include);
            }
            if (op_val == UPDATE_OPR &&
                get_extension(ON_UPDATE_INCLUDE, next, &on_update_include) == EXIT_SUCCESS) {
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

            } else if ((get_extension(GROUP_LIST_WITH_SEPARATOR, next, &group_list_separator) ==
                        EXIT_SUCCESS) &&
                       (get_extension(GROUP_LEAFS_VALUES_SEPARATOR, next,
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
            break;
        }
        LYD_TREE_DFS_END(startcmd_node, next);
    }
    return strdup(cmd_line);
}

char *lyd2cmd_line(struct lyd_node *startcmd_node, char *oper2cmd_prefix[3])
{
    oper_t op_val;
    char cmd_line[CMD_LINE_SIZE] = { 0 };

    // prepare for new command
    op_val = get_operation(startcmd_node);
    if (op_val == UNKNOWN_OPR) {
        fprintf(stderr, "%s: unknown operation for startcmd node \"%s\" \n", __func__,
                startcmd_node->schema->name);
        return NULL;
    }
    // add cmd prefix to the cmd_line
    strlcpy(cmd_line, oper2cmd_prefix[op_val], sizeof(cmd_line));
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

/**
 * duplicate lyd_node with its parent, then link the parent (insert_sibling)
 * to the current provided parent
 * @param parent [in,out] lyd_node parent tree to insert the child's parent to it.
 * @param child  [in]     lyd_node to be duplicated and linked to the parent tree.
 * @return EXIT_SUCCESS
 * @return EXIT_FAILURE
 */
int dup_and_insert_node(struct lyd_node **parent, struct lyd_node *child)
{
    int ret;
    struct lyd_node *child_dup = NULL;

    ret = lyd_dup_single(child, NULL, LYD_DUP_WITH_PARENTS | LYD_DUP_RECURSIVE | LYD_DUP_WITH_FLAGS,
                         &child_dup);
    if (ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to duplicate node `%s`: %s.\n", __func__, child->schema->name,
                ly_strerrcode(ret));
        return EXIT_FAILURE;
    }
    ret = lyd_insert_sibling(*parent, lyd_parent(child_dup), parent);
    if (ret != LY_SUCCESS) {
        fprintf(stderr, "%s: failed to insert sibling for node \"%s\": %s.\n", __func__,
                lyd_parent(child)->schema->name, ly_strerrcode(ret));
        lyd_free_all(child_dup);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/**
 * @brief get all leafrefs of startcmd node, and create
 * @param all_change_nodes [in]  all change nodes
 * @param startcmd_node     [in]  lyd_node to create cmd_info for and add it to cmds.
 * @param found_leafrefs   [out] all leafrefs founded, the leafrefs will be duplicates of the
 *                               original leafref, and their parents will linked together.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int get_node_leafrefs(const struct lyd_node *all_change_nodes, struct lyd_node *startcmd,
                      struct lyd_node **found_leafrefs)
{
    int ret;
    // in ->priv we add an int flag which indicate that this node has been processed.
    if (startcmd->priv != NULL)
        return EXIT_SUCCESS;
    struct lyd_node *next, *lefref_node = NULL;
    LYD_TREE_DFS_BEGIN(startcmd, next)
    {
        if (next->schema->nodetype == LYS_LEAF) {
            LY_DATA_TYPE type = ((struct lysc_node_leaf *)next->schema)->type->basetype;
            // union might have leafrefs.
            if (type == LY_TYPE_UNION) {
                struct lysc_type_union *y_union_t = NULL;
                struct ly_set *s_set;
                y_union_t = (struct lysc_type_union *)((struct lysc_node_leaf *)next->schema)->type;
                LY_ARRAY_COUNT_TYPE i_sized;
                LY_ARRAY_FOR(y_union_t->types, i_sized)
                {
                    // get the schema node of the leafref.
                    struct lysc_node *target_startcmd_y_node = NULL;

                    struct lysc_type_leafref *lref_t =
                        (struct lysc_type_leafref *)y_union_t->types[i_sized];
                    ret = lys_find_expr_atoms(next->schema, next->schema->module, lref_t->path,
                                              lref_t->prefixes, 0, &s_set);
                    if (s_set == NULL) {
                        printf("%s: failed to get target leafref for node \"%s\": %s\n", __func__,
                               next->schema->name, ly_strerrcode(ret));
                        return EXIT_FAILURE;
                    }
                    target_startcmd_y_node = s_set->snodes[s_set->count - 2];

                    char xpath[1024] = { 0 }, dxpath[1024] = { 0 };
                    // get the xpath of the schema node.
                    lysc_path(target_startcmd_y_node, LYSC_PATH_DATA_PATTERN, xpath, 1024);
                    // create data xpath for the schema node (add list predicate).
                    sprintf(dxpath, xpath, lyd_get_value(next));

                    ret = lyd_find_path(all_change_nodes, dxpath, 0, &lefref_node);

                    if (ret == LY_SUCCESS) {
                        if (dup_and_insert_node(found_leafrefs, lefref_node) != EXIT_SUCCESS) {
                            fprintf(stderr, "%s: failed to insert dependency leafref `%s`: %s.\n",
                                    __func__, lefref_node->schema->name, ly_strerrcode(ret));
                            return EXIT_FAILURE;
                        }
                    }
                }
            } else if (type == LY_TYPE_LEAFREF) {
                // get the schema node of the leafref.

                struct lysc_node *target_startcmd_y_node = NULL, *target_y_node = NULL;
                target_y_node = (struct lysc_node *)lysc_node_lref_target(next->schema);
                if (target_y_node == NULL) {
                    fprintf(stderr, "%s: failed to get target leafref for node \"%s\"\n", __func__,
                            next->schema->name);
                    return EXIT_FAILURE;
                }
                target_startcmd_y_node = target_y_node->parent;

                char xpath[1024] = { 0 }, dxpath[1024] = { 0 };
                // get the xpath of the schema node.
                lysc_path(target_startcmd_y_node, LYSC_PATH_DATA_PATTERN, xpath, 1024);
                // create data xpath for the schema node (add list predicate).
                sprintf(dxpath, xpath, lyd_get_value(next));

                ret = lyd_find_path(all_change_nodes, dxpath, 0, &lefref_node);

                if (ret == LY_SUCCESS) {
                    if (dup_and_insert_node(found_leafrefs, lefref_node) != EXIT_SUCCESS) {
                        fprintf(stderr, "%s: failed to insert dependency leafref `%s`: %s.\n",
                                __func__, lefref_node->schema->name, ly_strerrcode(ret));
                        return EXIT_FAILURE;
                    }
                }
            }
        }
        LYD_TREE_DFS_END(startcmd, next);
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
    // if node already processed skip.
    if (startcmd_node->priv != NULL)
        return EXIT_SUCCESS;

    int ret = EXIT_SUCCESS;
    char *oper2cmd_prefix[3] = { NULL };
    char *cmd_line = NULL, *rollback_cmd_line = NULL;
    struct lyd_node *rollback_dnode = NULL;
    // first get the add, update, delete cmds prefixis from schema extensions
    if (get_extension(CMD_ADD_EXT, lyd_parent(startcmd_node), &oper2cmd_prefix[ADD_OPR]) !=
        EXIT_SUCCESS) {
        fprintf(stderr,
                "%s: cmd-add extension is missing from root container "
                "make sure root container has ipr2cgen:cmd-add\n",
                __func__);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    if (get_extension(CMD_DELETE_EXT, lyd_parent(startcmd_node), &oper2cmd_prefix[DELETE_OPR]) !=
        EXIT_SUCCESS) {
        fprintf(stderr,
                "%s: cmd-delete extension is missing from root container "
                "make sure root container has ipr2cgen:cmd-delete\n",
                __func__);
        ret = EXIT_FAILURE;
        goto cleanup;
    }
    if (get_extension(CMD_UPDATE_EXT, lyd_parent(startcmd_node), &oper2cmd_prefix[UPDATE_OPR]) !=
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
    // get the rollback node.
    lyd_diff_reverse_all(startcmd_node, &rollback_dnode);

    // the reversed node come with no parent, so we need to duplicate startcmd parent, and insert
    // the rollback_node into the duplicated parent.
    // this is needed when fetching data from sr, otherwise the fetch will fail.
    struct lyd_node *rollback_dnode_parent = NULL;
    lyd_dup_single(lyd_parent(startcmd_node), NULL, LYD_DUP_WITH_FLAGS, &rollback_dnode_parent);
    lyd_insert_child(rollback_dnode_parent, rollback_dnode);

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
 * add all dependencies for node startcmd_node, this add dependencies recursively.
 * @param cmds [in,out] array of cmd_info
 * @param cmd_idx [in,out] current index pointer for cmds
 * @param all_change_nodes [in] all change nodes
 * @param startcmd_node [in] startcmd_node to find and add its dependencies.
 * @return EXIST_SUCCESS
 * @return EXIST_FAILURE
 */
int add_dependencies_cmd_info(struct cmd_info **cmds, int *cmd_idx,
                              const struct lyd_node *all_change_nodes,
                              struct lyd_node *startcmd_node)
{
    int ret = EXIT_SUCCESS;
    struct lyd_node *node_leafrefs = NULL;

    // check if node is already processed
    if (startcmd_node->priv != NULL)
        return EXIT_SUCCESS;
    // get the node leafrefs
    ret = get_node_leafrefs(all_change_nodes, startcmd_node, &node_leafrefs);
    if (ret != EXIT_SUCCESS) {
        fprintf(stderr, "%s: failed to get the leafref depenedecnies for node \"%s\"\n ", __func__,
                startcmd_node->schema->name);
        return EXIT_FAILURE;
    }
    // check if dependencies found.
    if (node_leafrefs != NULL) {
        struct lyd_node *leafref_node;
        LY_LIST_FOR(node_leafrefs, leafref_node)
        {
            // - if the start_cmd node's operation is delete, then add it before it's leafref.
            // - if the start_cmd node's operation is add, then add leafref before
            if (get_operation(startcmd_node) == DELETE_OPR) {
                if (startcmd_node->priv == NULL) {
                    ret = add_cmd_info_core(cmds, cmd_idx, startcmd_node);
                    if (ret != EXIT_SUCCESS)
                        goto cleanup;
                    startcmd_node->priv = &processed;
                }
            } else {
                // first get the original leafref node that reside in all_change_nodes, as we need to
                // change it's ->priv value to processed.
                char dxpath[1024] = { 0 };
                struct lyd_node *original_leafref_dnode = NULL;
                lyd_path(lyd_child(leafref_node), LYD_PATH_STD, dxpath, 1024);
                ret = lyd_find_path(all_change_nodes, dxpath, 0, &original_leafref_dnode);
                if (ret != LY_SUCCESS) {
                    fprintf(stderr, "%s: failed to get the original leafref node \"%s\"\n ",
                            __func__, startcmd_node->schema->name);
                    ret = EXIT_FAILURE;
                    goto cleanup;
                }
                // check if the found dependency also has dependencies and add it.
                ret = add_dependencies_cmd_info(cmds, cmd_idx, all_change_nodes,
                                                original_leafref_dnode);
                if (ret != EXIT_SUCCESS)
                    goto cleanup;

                if (original_leafref_dnode != NULL) {
                    ret = add_cmd_info_core(cmds, cmd_idx, original_leafref_dnode);
                    if (ret != EXIT_SUCCESS)
                        goto cleanup;
                    original_leafref_dnode->priv = &processed;
                }
            }
        }
    }
cleanup:
    if (node_leafrefs)
        lyd_free_all(node_leafrefs);
    return ret;
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
    struct lyd_node *next = NULL, *child_node = NULL;
    // initialize ->priv to NULL.
    LY_LIST_FOR(all_change_nodes, change_node)
    {
        child_node = lyd_child(change_node);

        lyd_print_mem(&node_print_text, change_node, LYD_XML, 0);
        fprintf(stdout, "--%s", node_print_text);
        free(node_print_text);

        LY_LIST_FOR(child_node, next)
        {
            if (next->schema->nodetype == LYS_LIST && is_startcmd_node(next)) {
                next->priv = NULL;
            }
        }
    }

    // loop through all modules and add all dependencies first.
    LY_LIST_FOR(all_change_nodes, change_node)
    {
        child_node = lyd_child(change_node);
        //
        LY_LIST_FOR(child_node, next)
        {
            if (next->schema->nodetype == LYS_LIST && is_startcmd_node(next)) {
                int ret = add_dependencies_cmd_info(cmds, &cmd_idx, all_change_nodes, next);
                if (ret != EXIT_SUCCESS) {
                    free_cmds_info(cmds);
                    return NULL;
                }
            }
        }
    }

    // loop through all modules again and add all none processed nodes (->priv == NULL).
    LY_LIST_FOR(all_change_nodes, change_node)
    {
        child_node = lyd_child(change_node);
        // add none dependencies (->priv == NULL) second.
        LY_LIST_FOR(child_node, next)
        {
            if (next->schema->nodetype == LYS_LIST && is_startcmd_node(next)) {
                // check if node is already added (through add_dependencies_cmd_info())
                if (next->priv == NULL) {
                    if (add_cmd_info_core(cmds, &cmd_idx, next) != EXIT_SUCCESS) {
                        free_cmds_info(cmds);
                        return NULL;
                    }
                    next->priv = &processed;
                }
            }
        }
    }

    return cmds;
}
