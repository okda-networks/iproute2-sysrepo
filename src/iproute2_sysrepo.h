#ifndef IPROUTE2_SYSREPO_H
#define IPROUTE2_SYSREPO_H

#include <sysrepo.h>

extern sr_session_ctx_t *sr_session;
extern sr_conn_ctx_t *sr_connection;
extern sr_subscription_ctx_t *sr_sub_ctx;

#endif // IPROUTE2_SYSREPO_H