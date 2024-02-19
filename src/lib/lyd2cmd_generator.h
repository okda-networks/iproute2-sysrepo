/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IPROUTE2_SYSREPO_LYD2CMD_GENERATOR_H
#define IPROUTE2_SYSREPO_LYD2CMD_GENERATOR_H

#include <libyang/libyang.h>

#define CMDS_ARRAY_SIZE 1024

/**
 * @brief data struct to store the int argc,char **argv pair.
 *
 * this struct can be easily used by
 * iproute2::do_cmd(argc,argv)
 */
struct cmd_args{
  int argc;
  char **argv;
};

/**
 * @brief generate argc,argv commands out of the lyd_node (diff).
 * this lyd_node can be obtained from sysrepo API sr_get_change_diff(session) or libyang's lyd_lyd_diff_tree()
 *
 * @param[in] node Data node diff to generate the argc, argv.
 * @return Array of cmd_args struct.
 */
struct cmd_args** generate_cmd_argv(const struct lyd_node *change_node);

#endif// IPROUTE2_SYSREPO_LYD2CMD_GENERATOR_H
