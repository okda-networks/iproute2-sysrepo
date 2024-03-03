/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * json_print2.c	 - memory ouput instead of stdout
 *
 * Authors:    Vincent Jardin, <vjardin@free.fr>
 */

#include <stdarg.h>
#include <stdio.h>

#include "utils.h"
#include "json_print.h"

FILE *j_stream = NULL;
char json_buffer[1024 * 1024] = { 0 };
static json_writer_t *_jw;

static void __new_json_obj_mem(int json, bool have_array)
{
	if (json) {
		j_stream = fmemopen(json_buffer, sizeof(json_buffer), "w");
		_jw = jsonw_new(j_stream);
		if (!_jw) {
			perror("json object");
			exit(1);
		}
		if (pretty)
			jsonw_pretty(_jw, true);
		if (have_array)
			jsonw_start_array(_jw);
	}
}

static void __delete_json_obj_mem(bool have_array)
{
	if (_jw) {
		if (have_array)
			jsonw_end_array(_jw);
		jsonw_destroy(&_jw);
		fclose(j_stream);
	}
}

void new_json_obj(int json)
{
	__new_json_obj_mem(json, true);
}

void delete_json_obj(void)
{
	__delete_json_obj_mem(true);
}

void new_json_obj_plain(int json)
{
	__new_json_obj_mem(json, false);
}

void delete_json_obj_plain(void)
{
	__delete_json_obj_mem(false);
}

