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

char *yang_ext_map[] = {
        [CMD_START_EXT] = "cmd-start",
        [CMD_ADD_EXT] = "cmd-add",
        [CMD_DELETE_EXT] = "cmd-delete",
        [CMD_UPDATE_EXT] = "cmd-update",

        // leaf extensions
        [ARG_NAME_EXT] = "arg-name",
        [FLAG_EXT] = "flag",
        [VALUE_ONLY_EXT] = "value-only",
        [VALUE_ONLY_ON_UPDATE_EXT] = "value-only-on-update",
};

void dup_argv(char ***dest, char **src, int argc)
{
    *dest = (char **) malloc(argc * sizeof(char *));
    if (*dest == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < argc; i++) {
        (*dest)[i] = strdup(src[i]);
        if ((*dest)[i] == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
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
            // Free the argv array itself
            free(cmds_info[i]->argv);
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
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }

        // Copy the substring after the colon.
        strlcpy(output, colon_pos + 1, len + 1);

        // Remove leading and trailing whitespace
        char *end = output + len - 1;
        while (end > output && isspace((unsigned char) *end)) {
            *end-- = '\0';
        }
        while (*output && isspace((unsigned char) *output)) {
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
    struct lyd_meta *next;
    const char *operation;
    LY_LIST_FOR(dnode->meta, next)
    {
        if (!strcmp("operation", next->name)) {
            operation = lyd_get_meta_value(next);
            if (!strcmp("create", operation))
                return ADD_OPR;
            if (!strcmp("delete", operation))
                return DELETE_OPR;
            if (!strcmp("replace", operation) || !strcmp("none", operation))// for updated list the operation is none.
                return UPDATE_OPR;
        }
    }
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
    *argv = (char **) malloc((*argc) * sizeof(char *));
    if (*argv == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    // Tokenize the command string and populate argv
    char *token;
    int i = 0;
    char *cmd_copy = strdup(command);// We duplicate the command string as strtok modifies the original string
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
 * @param [in] cmd_line command line string "ip link add name lo0 type dummy"
 * @param [in] start_dnode start_cmd node to be added to the cmd_info struct.
 * @param [in,out] cmds array of cmd_info.
 * @param [in,out] cmd_idx current index of cmds.
 */
void add_command(char *cmd_line,const struct lyd_node *start_dnode,
                 struct cmd_info **cmds, int *cmd_idx)
{
    int argc;
    char **argv;
    parse_command(cmd_line, &argc, &argv);
    if (*cmd_idx >= CMDS_ARRAY_SIZE) {
        fprintf(stderr, "process_command: cmds exceeded MAX allowed commands per transaction\n");
        free_argv(argv, argc);
        return;
    }
    (cmds)[*cmd_idx] = malloc(sizeof(struct cmd_info));

    if ((cmds)[*cmd_idx] == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    dup_argv(&((cmds)[*cmd_idx]->argv), argv, argc);
    (cmds)[*cmd_idx]->argc = argc;
    (cmds)[*cmd_idx]->cmd_start_dnode = start_dnode;
    (*cmd_idx)++;
    memset(cmd_line, 0, CMD_LINE_SIZE);
    free_argv(argv, argc);
}

struct cmd_info **lyd2cmds(const struct lyd_node *change_node)
{
    char *result;
    int cmd_idx = 0;
    char cmd_line[CMD_LINE_SIZE] = {0};

    lyd_print_mem(&result, change_node, LYD_XML, 0);
    printf("--%s", result);

    struct cmd_info **cmds = malloc(CMDS_ARRAY_SIZE * sizeof(struct cmd_info *));
    if (cmds == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    // Set all elements of the array to NULL
    for (int i = 0; i < CMDS_ARRAY_SIZE; i++) {
        cmds[i] = NULL;
    }

    // this will hold the add, update and delete cmd prefixes.
    char *oper2cmd_prefix[3] = {NULL};

    // first get the add, update, delete cmds prefixis from schema extensions
    if (get_extension(CMD_ADD_EXT, change_node, &oper2cmd_prefix[ADD_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: cmd-add extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-add\n",
                __func__);
        return NULL;
    }
    if (get_extension(CMD_DELETE_EXT, change_node, &oper2cmd_prefix[DELETE_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: cmd-delete extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-delete\n",
                __func__);
        return NULL;
    }
    if (get_extension(CMD_UPDATE_EXT, change_node, &oper2cmd_prefix[UPDATE_OPR]) != EXIT_SUCCESS) {
        fprintf(stderr, "%s: ipr2cgen:cmd-update extension is missing from root container "
                        "make sure root container has ipr2cgen:cmd-update\n",
                __func__);
        return NULL;
    }

    oper_t op_val;
    struct lyd_node *next, *startcmd_node;
    LYD_TREE_DFS_BEGIN(change_node, next)
    {
        LY_DATA_TYPE type;
        // if this is startcmd node (schema has ipr2cgen:cmd-start), then start building a new command
        if (is_startcmd_node(next)) {
            startcmd_node = next;
            // check if the cmd is not empty (first cmd)
            if (cmd_line[0] != 0)
                add_command(cmd_line, startcmd_node, cmds, &cmd_idx);


            // prepare for new command
            op_val = get_operation(next);
            if (op_val == UNKNOWN_OPR) {
                fprintf(stderr, "%s: unknown operation for startcmd node \"%s\" \n",
                        __func__, next->schema->name);
                free_cmds_info(cmds);
                return NULL;
            }
            strlcpy(cmd_line, oper2cmd_prefix[op_val], sizeof(cmd_line));
        } else if (next->schema->nodetype == LYS_LEAF) {
            char *key = NULL, *value = NULL;

            // if list operation is delete, get the keys only
            if (op_val == DELETE_OPR && !lysc_is_key(next->schema))
                goto next_iter;

            // if FLAG extension, add schema name to the cmd and go to next iter.
            if (get_extension(FLAG_EXT, next, NULL) == EXIT_SUCCESS) {
                if (!strcmp("true", lyd_get_value(next)))
                    key = strdup(next->schema->name);
                goto next_iter;
            }

            // if value only extension, don't include the schema->name (key) in the command.
            if (get_extension(VALUE_ONLY_EXT, next, NULL) == EXIT_SUCCESS);
             // if arg_name extension, key will be set to the arg-name value.
            else if (get_extension(ARG_NAME_EXT, next, &key) == EXIT_SUCCESS) {
                if (key == NULL) {
                    fprintf(stderr, "%s: ipr2cgen:arg-name extension found but failed to "
                                    "get the arg-name value for node \"%s\"\n",
                            __func__, next->schema->name);
                    free_cmds_info(cmds);
                    return NULL;
                }
            } else
                key = strdup(next->schema->name);

            // set the value
            // special case for identity leaf, where the module prefix might be added (e.g iproute-link:vti)
            type = ((struct lysc_node_leaf *) next->schema)->type->basetype;
            if (type == LY_TYPE_IDENT)
                value = strip_yang_iden_prefix(lyd_get_value(next));
            else
                value = (char *) lyd_get_value(next);
        next_iter:
            if (key != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, key, sizeof(cmd_line));
            }
            if (value != NULL) {
                strlcat(cmd_line, " ", sizeof(cmd_line));
                strlcat(cmd_line, value, sizeof(cmd_line));
            }
        }
        LYD_TREE_DFS_END(change_node, next);
    }
    if (cmd_line[0] != 0)
        add_command(cmd_line,startcmd_node,cmds,&cmd_idx);

    return cmds;
}
