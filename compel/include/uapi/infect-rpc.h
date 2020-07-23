#ifndef __COMPEL_INFECT_RPC_H__
#define __COMPEL_INFECT_RPC_H__

#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>

#include "common/lock.h"

struct parasite_ctl;
struct parasite_cmd_args;
extern int __must_check compel_rpc_sync(unsigned int cmd, struct parasite_ctl *ctl);
extern int __must_check compel_rpc_call(unsigned int cmd, struct parasite_ctl *ctl);
extern int __must_check compel_rpc_call_sync(unsigned int cmd, struct parasite_ctl *ctl);
extern int compel_rpc_sock(struct parasite_ctl *ctl);

struct parasite_cmd_args {
	futex_t		cmd;			/* command itself */
	uint32_t	ack;			/* ack on command */
	int32_t		err;			/* error code on reply */
};

#define PARASITE_USER_CMDS	64


#endif
