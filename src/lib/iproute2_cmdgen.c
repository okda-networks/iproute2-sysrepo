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

#include "iproute2_cmdgen.h"

#define CMD_LINE_SIZE 1024

typedef enum {
    ADD,
    DELETE,
    UPDATE,
    UNKNOWN,
} oper_t;


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

/*
 * if input is "iproute2-ip-link:dummy" it return "dummy"
 */
char *strip_yang_iden_prefix(const char *input)
{
    const char *colon_pos = strchr(input, ':');
    if (colon_pos != NULL) {
        // Allocate memory for output string
        char *output = malloc(strlen(colon_pos + 1) + 1);
        if (output == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        strcpy(output, colon_pos + 1);
        // Remove leading and trailing whitespace
        char *end = output + strlen(output) - 1;
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


oper_t get_operation(struct lyd_node *dnode)
{

    struct lyd_meta *next;
    const char *operation;
    LY_LIST_FOR(dnode->meta, next)
    {
        if (!strcmp("operation", next->name)) {
            operation = lyd_get_meta_value(next);
            if (!strcmp("create", operation))
                return ADD;
            if (!strcmp("delete", operation))
                return DELETE;
            if (!strcmp("replace", operation) || !strcmp("none", operation))// for updated list the operation is none.
                return UPDATE;
        }
    }
    return UNKNOWN;
}

/*
 * return true if the node has ir2cgen:cmd-start extension.
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

/*
 * this function will convert the command line string to argc, argv
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

void add_command(char *cmd_line, struct cmd_args **cmds, int *cmd_idx, char **oper2cmd_prefix)
{
    int argc;
    char **argv;
    parse_command(cmd_line, &argc, &argv);
    if (*cmd_idx >= CMDS_ARRAY_SIZE) {
        fprintf(stderr, "process_command: cmds exceeded MAX allowed commands per transaction\n");
        free_argv(argv, argc);
        return;
    }
    (cmds)[*cmd_idx] = malloc(sizeof(struct cmd_args));

    if ((cmds)[*cmd_idx] == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    dup_argv(&((cmds)[*cmd_idx]->argv), argv, argc);
    (cmds)[*cmd_idx]->argc = argc;
    (*cmd_idx)++;
    memset(cmd_line, 0, CMD_LINE_SIZE);
    free_argv(argv, argc);
}

// not implemented yet
struct cmd_args **lyd2cmd_argv(const struct lyd_node *change_node)
{
    char *result;
    lyd_print_mem(&result, change_node, LYD_XML, 0);
    printf("--%s", result);

    struct cmd_args **cmds = malloc(CMDS_ARRAY_SIZE * sizeof(struct cmd_args *));
    if (cmds == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    int cmd_idx = 0;
    char cmd_line[CMD_LINE_SIZE] = {0};

    // this will hold the add, update and delete cmd prefixes.
    char *oper2cmd_prefix[3] = {NULL};

    // first get the add, update, delete cmds prefixis from schema extensions
    const struct lysc_node *schema = change_node->schema;
    LY_ARRAY_COUNT_TYPE i;
    LY_ARRAY_FOR(schema->exts, i)
    {
        if (schema->exts != NULL) {
            if (!strcmp(schema->exts[i].def->name, "cmd-add"))
                oper2cmd_prefix[ADD] = strdup(schema->exts[i].argument);
            else if (!strcmp(schema->exts[i].def->name, "cmd-update"))
                oper2cmd_prefix[UPDATE] = strdup(schema->exts[i].argument);
            else if (!strcmp(schema->exts[i].def->name, "cmd-delete"))
                oper2cmd_prefix[DELETE] = strdup(schema->exts[i].argument);
        }
    }

    if (oper2cmd_prefix[ADD] == NULL || oper2cmd_prefix[UPDATE] == NULL || oper2cmd_prefix[DELETE] == NULL) {
        fprintf(stderr, "%s: one or more required cmd extension not found, "
                        "make sure root container has ir2cgen:cmd-add, ir2cgen:cmd-update "
                        "and ir2cgen:cmd-delete extensions\n",
                __func__);
        return NULL;
    }

    oper_t op_val;
    struct lyd_node *next;
    LYD_TREE_DFS_BEGIN(change_node, next)
    {
        // if this is startcmd node (schema has ir2cgen:cmd-start), then start building a new command
        if (is_startcmd_node(next)) {
            // check if the cmd is not empty (first cmd)
            if (cmd_line[0] != 0)
                add_command(cmd_line, cmds, &cmd_idx, oper2cmd_prefix);

            // prepare for new command
            op_val = get_operation(next);
            if (op_val == UNKNOWN) {
                fprintf(stderr, "%s: unknown operation for startcmd node (%s) \n",
                        __func__, next->schema->name);
                return NULL;
            }
            strcpy(cmd_line, oper2cmd_prefix[op_val]);
        } else if (next->schema->nodetype != LYS_CONTAINER) {
            // not a startcmd, get key-value pair and apped to cmd_line
            char *value = (char *) lyd_get_value(next);
            strcat(cmd_line, " ");
            strcat(cmd_line, next->schema->name);
            strcat(cmd_line, " ");
            if (next->schema->nodetype == LYS_LEAF) {
                LY_DATA_TYPE type = ((struct lysc_node_leaf *) next->schema)->type->basetype;
                // special case for identity leaf, where the module prefix might be added (e.g iproute-link:vti)
                                    if (type == LY_TYPE_IDENT)
                                        value = strip_yang_iden_prefix(lyd_get_value(next));
            }
            strcat(cmd_line, value);
        }
        LYD_TREE_DFS_END(change_node, next);
    }
    if (cmd_line[0] != 0)
        add_command(cmd_line, cmds, &cmd_idx, oper2cmd_prefix);

    return cmds;
}
