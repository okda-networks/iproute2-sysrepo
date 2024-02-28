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

char *yang_ext_map[] = { [CMD_START_EXT] = "cmd-start",
			 [CMD_ADD_EXT] = "cmd-add",
			 [CMD_DELETE_EXT] = "cmd-delete",
			 [CMD_UPDATE_EXT] = "cmd-update",
			 [GROUP_LIST_WITH_SEPARATOR] =
				 "group-list-with-separator",
			 [GROUP_LEAFS_VALUES_SEPARATOR] =
				 "group-leafs-values-separator",

			 // leaf extensions
			 [ARG_NAME_EXT] = "arg-name",
			 [FLAG_EXT] = "flag",
			 [VALUE_ONLY_EXT] = "value-only",
			 [VALUE_ONLY_ON_UPDATE_EXT] = "value-only-on-update",
			 [ON_UPDATE_INCLUDE] = "on-update-include" };

void dup_argv(char ***dest, char **src, int argc)
{
	*dest = (char **)malloc(argc * sizeof(char *));
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
			if (!strcmp("replace", operation) ||
			    !strcmp("none",
				    operation)) // for updated list the operation is none.
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
	*argv = (char **)malloc((*argc) * sizeof(char *));
	if (*argv == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	// Tokenize the command string and populate argv
	char *token;
	int i = 0;
	char *cmd_copy = strdup(
		command); // We duplicate the command string as strtok modifies the original string
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
 * @param [in] start_dnode start_cmd node to be added to the cmd_info struct.
 */
void add_command(struct cmd_info **cmds, int *cmd_idx, char *cmd_line,
		 const struct lyd_node *start_dnode)
{
	int argc;
	char **argv;
	parse_command(cmd_line, &argc, &argv);
	if (*cmd_idx >= CMDS_ARRAY_SIZE) {
		fprintf(stderr,
			"process_command: cmds exceeded MAX allowed commands per transaction\n");
		free_argv(argv, argc);
		return;
	}
	(cmds)[*cmd_idx] = malloc(sizeof(struct cmd_info));

	if ((cmds)[*cmd_idx] == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		free_argv(argv, argc);
		return;
	}
	dup_argv(&((cmds)[*cmd_idx]->argv), argv, argc);
	(cmds)[*cmd_idx]->argc = argc;
	(cmds)[*cmd_idx]->cmd_start_dnode = start_dnode;
	(*cmd_idx)++;
	memset(cmd_line, 0, CMD_LINE_SIZE);
	free_argv(argv, argc);
}

/**
 * create argument name from dnoe
 * @param [in] dnode lyd_node
 * @param [in] curr_op_val starnode op_val
 * @param [out] value the captured arg name
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int creat_cmd_arg_name(struct lyd_node *dnode, oper_t curr_op_val, char **key)
{
	// if list operation is delete, get the keys only
	if (curr_op_val == DELETE_OPR && !lysc_is_key(dnode->schema))
		return EXIT_SUCCESS;

	if (get_extension(FLAG_EXT, dnode, NULL) == EXIT_SUCCESS) {
		if (!strcmp("true", lyd_get_value(dnode)))
			*key = strdup(dnode->schema->name);
		return EXIT_SUCCESS;
	}

	if (get_extension(VALUE_ONLY_EXT, dnode, NULL) == EXIT_SUCCESS)
		return EXIT_SUCCESS;
	if (get_extension(ARG_NAME_EXT, dnode, key) == EXIT_SUCCESS) {
		if (*key == NULL) {
			fprintf(stderr,
				"%s: ipr2cgen:arg-name extension found but failed to "
				"get the arg-name value for node \"%s\"\n",
				__func__, dnode->schema->name);
			return EXIT_FAILURE;
		}
	} else
		*key = strdup(dnode->schema->name);
	return EXIT_SUCCESS;
}

/**
 * create argument value from dnoe
 * @param [in] dnode lyd_node
 * @param [in] curr_op_val starnode op_val
 * @param [out] value the captured arg value value
 */
void creat_cmd_arg_value(struct lyd_node *dnode, oper_t curr_op_val,
			 char **value)
{
	// if list operation is delete, get the keys only
	if (curr_op_val == DELETE_OPR && !lysc_is_key(dnode->schema))
		return;

	// if FLAG extension, add schema name to the cmd and go to next iter.
	if (get_extension(FLAG_EXT, dnode, NULL) == EXIT_SUCCESS) {
		return;
	}
	if (value == NULL)
		value = malloc(sizeof(char *));
	// if list and grouping group the values.
	if (dnode->schema->nodetype == LYS_LIST) {
		char *group_list_separator = NULL;
		char *group_leafs_values_separator = NULL;
		if ((get_extension(GROUP_LIST_WITH_SEPARATOR, dnode,
				   &group_list_separator) == EXIT_SUCCESS) &&
		    (get_extension(GROUP_LEAFS_VALUES_SEPARATOR, dnode,
				   &group_leafs_values_separator) ==
		     EXIT_SUCCESS)) {
			// add space
			char temp_value[50] = { 0 };

			strlcat(temp_value, " ", sizeof(temp_value));
			struct lyd_node *list_first_entry =
				lyd_first_sibling(dnode);
			struct lyd_node *list_next;

			// loop through the list.
			LY_LIST_FOR(list_first_entry, list_next)
			{
				struct lyd_node *list_first_leaf =
					lyd_child(list_next);
				struct lyd_node *leaf_next = NULL;
				// loop through the list entries and add them to cmd_line with the specified separator
				LY_LIST_FOR(list_first_leaf, leaf_next)
				{
					strlcat(temp_value,
						lyd_get_value(leaf_next),
						sizeof(temp_value));
					if (leaf_next->next != NULL)
						strlcat(temp_value,
							group_leafs_values_separator,
							sizeof(temp_value));
				}
				// add the list separator.
				if (list_next->next != NULL &&
				    !strcmp(list_next->next->schema->name,
					    list_next->schema->name))
					strlcat(temp_value,
						group_list_separator,
						sizeof(temp_value));
			}
			*value = strdup(temp_value);
		}
	} else {
		// if INDENT type remove the module name from the value (example: ip-link-type:dummy)
		// this strip ip-link-type.
		LY_DATA_TYPE type =
			((struct lysc_node_leaf *)dnode->schema)->type->basetype;
		if (type == LY_TYPE_IDENT)
			*value = strip_yang_iden_prefix(lyd_get_value(dnode));
		else
			*value = (char *)strdup(lyd_get_value(dnode));
	}
}

/**
 * get the target node from sysrepo running ds,
 * @param  [in] startcmd_node start cmd_node,
 * @param  [in] node_name node name to be fetched from sr.
 * @return [out] lyd_node found.
 */
struct lyd_node *get_node_from_sr(const struct lyd_node *startcmd_node,
				  char *node_name)
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
		char *on_update_include = NULL;
		char *group_list_separator = NULL;
		char *group_leafs_values_separator = NULL;
		char *key = NULL, *value = NULL;
		switch (next->schema->nodetype) {
		case LYS_LIST:
		case LYS_CONTAINER:
			if (op_val == UPDATE_OPR &&
			    get_extension(ON_UPDATE_INCLUDE, next,
					  &on_update_include) == EXIT_SUCCESS) {
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
					key = NULL;
					value = NULL;

					struct lyd_node *include_node =
						get_node_from_sr(startcmd_node,
								 token);
					if (include_node == NULL)
						return NULL;

					creat_cmd_arg_value(include_node,
							    op_val, &value);
					ret = creat_cmd_arg_name(include_node,
								 op_val, &key);
					if (ret != EXIT_SUCCESS) {
						fprintf(stderr,
							"%s: failed to create creat_cmd_arg_name for node \"%s\".\n",
							__func__,
							next->schema->name);
						return NULL;
					}

					if (key != NULL) {
						strlcat(cmd_line, " ",
							sizeof(cmd_line));
						strlcat(cmd_line, key,
							sizeof(cmd_line));
						free(key);
					}
					if (value != NULL) {
						strlcat(cmd_line, " ",
							sizeof(cmd_line));
						strlcat(cmd_line, value,
							sizeof(cmd_line));
						free(value);
					}
					// get next token.
					token = strtok(NULL, ",");
				}
				break;

			} else if ((get_extension(GROUP_LIST_WITH_SEPARATOR,
						  next,
						  &group_list_separator) ==
				    EXIT_SUCCESS) &&
				   (get_extension(
					    GROUP_LEAFS_VALUES_SEPARATOR, next,
					    &group_leafs_values_separator) ==
				    EXIT_SUCCESS)) {
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
				creat_cmd_arg_value(next, op_val, &value);
				ret = creat_cmd_arg_name(next, op_val, &key);
				if (ret != EXIT_SUCCESS) {
					fprintf(stderr,
						"%s: failed to create creat_cmd_arg_name for node \"%s\".\n",
						__func__, next->schema->name);
					return NULL;
				}
				if (key != NULL) {
					strlcat(cmd_line, " ",
						sizeof(cmd_line));
					strlcat(cmd_line, key,
						sizeof(cmd_line));
					free(key);
				}
				if (value != NULL) {
					strlcat(cmd_line, " ",
						sizeof(cmd_line));
					strlcat(cmd_line, value,
						sizeof(cmd_line));
					free(value);
				}
				const char *grouped_schema_name =
					next->schema->name;
				// skip the collected list info. while the next is not null and the next node is
				// same node schema name, then move next.
				while (next->next != NULL) {
					if (!strcmp(next->schema->name,
						    grouped_schema_name))
						next = next->next;
					else
						break;
				}
			}
			// next might be skipped,
			// if after skip, the node type of next is leaf or leaflist then continue to
			// leaf and leaflist cases to capture the arg_name arg_value for that node.
			if (next->schema->nodetype != LYS_LEAF &&
			    next->schema->nodetype != LYS_LEAFLIST)
				break;
		case LYS_LEAF:
		case LYS_LEAFLIST:

			key = NULL;
			value = NULL;
			creat_cmd_arg_value(next, op_val, &value);
			ret = creat_cmd_arg_name(next, op_val, &key);
			if (ret != EXIT_SUCCESS) {
				fprintf(stderr,
					"%s: failed to create creat_cmd_arg_name for node \"%s\".\n",
					__func__, next->schema->name);
				return NULL;
			}
			if (key != NULL) {
				strlcat(cmd_line, " ", sizeof(cmd_line));
				strlcat(cmd_line, key, sizeof(cmd_line));
				free(key);
			}
			if (value != NULL) {
				strlcat(cmd_line, " ", sizeof(cmd_line));
				strlcat(cmd_line, value, sizeof(cmd_line));
				free(value);
			}
			break;
		}
		LYD_TREE_DFS_END(startcmd_node, next);
	}
	return strdup(cmd_line);
}

struct cmd_info **lyd2cmds(const struct lyd_node *change_node)
{
	char *result;
	int cmd_idx = 0;
	char cmd_line[CMD_LINE_SIZE] = { 0 };

	lyd_print_mem(&result, change_node, LYD_XML, 0);
	printf("--%s", result);

	struct cmd_info **cmds =
		malloc(CMDS_ARRAY_SIZE * sizeof(struct cmd_info *));
	if (cmds == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return NULL;
	}

	// Set all elements of the array to NULL
	for (int i = 0; i < CMDS_ARRAY_SIZE; i++) {
		cmds[i] = NULL;
	}

	// this will hold the add, update and delete cmd prefixes.
	char *oper2cmd_prefix[3] = { NULL };

	// first get the add, update, delete cmds prefixis from schema extensions
	if (get_extension(CMD_ADD_EXT, change_node,
			  &oper2cmd_prefix[ADD_OPR]) != EXIT_SUCCESS) {
		fprintf(stderr,
			"%s: cmd-add extension is missing from root container "
			"make sure root container has ipr2cgen:cmd-add\n",
			__func__);
		return NULL;
	}
	if (get_extension(CMD_DELETE_EXT, change_node,
			  &oper2cmd_prefix[DELETE_OPR]) != EXIT_SUCCESS) {
		fprintf(stderr,
			"%s: cmd-delete extension is missing from root container "
			"make sure root container has ipr2cgen:cmd-delete\n",
			__func__);
		return NULL;
	}
	if (get_extension(CMD_UPDATE_EXT, change_node,
			  &oper2cmd_prefix[UPDATE_OPR]) != EXIT_SUCCESS) {
		fprintf(stderr,
			"%s: ipr2cgen:cmd-update extension is missing from root container "
			"make sure root container has ipr2cgen:cmd-update\n",
			__func__);
		return NULL;
	}

	oper_t op_val;
	struct lyd_node *next, *startcmd_node, *child_node;
	child_node = lyd_child(change_node);
	// loop through children of the root node.
	LY_LIST_FOR(child_node, next)
	{
		if (next->schema->nodetype == LYS_LIST) {
			if (is_startcmd_node(next)) { // start node
				startcmd_node = next;
				// prepare for new command
				op_val = get_operation(startcmd_node);
				if (op_val == UNKNOWN_OPR) {
					fprintf(stderr,
						"%s: unknown operation for startcmd node \"%s\" \n",
						__func__,
						startcmd_node->schema->name);
					free_cmds_info(cmds);
					return NULL;
				}
				// add cmd prefix to the cmd_line
				strlcpy(cmd_line, oper2cmd_prefix[op_val],
					sizeof(cmd_line));
				// get the cmd args for the startcmd_node
				char *cmd_args =
					lyd2cmdline_args(startcmd_node, op_val);
				if (cmd_args == NULL) {
					fprintf(stderr,
						"%s: failed to create cmdline arguments for node \"%s\" \n",
						__func__, next->schema->name);
					free_cmds_info(cmds);
					return NULL;
				}
				strlcat(cmd_line, cmd_args, sizeof(cmd_line));
				add_command(cmds, &cmd_idx, cmd_line,
					    startcmd_node);
				free(cmd_args);
			}
		}
	}
	return cmds;
}