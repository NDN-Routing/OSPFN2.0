// header file for opsfn.c containing definitions of functions and variables
// Author: Hoque

#ifndef _OSPFN_H_
#define _OSPFN_H_

#include "thread.h"
//#include "ospfclient/ospf_apiclient.h"
#include "tables.h"

#include <ccn/ccn.h>
#include <ccn/uri.h>
#include <ccn/face_mgmt.h>
#include <ccn/reg_mgmt.h>
#include <ccn/charbuf.h>


struct hop
{
	struct in_addr address;
	u_int32_t pref_order;
};

struct multipath
{
	struct hop nexthop;
	struct multipath *next;
};

struct ospfn
{
	struct ospf_apiclient *oclient;
	
	struct hash *prefix_table;
	struct hash *origin_table;
	
	struct ccn_charbuf *local_scope_template;
	unsigned char *ccndid;
	size_t ccndid_size;
	
	struct multipath *mp;
	char *logFile;
	char *loggingDir;
	u_int8_t CCN_NAME_TYPE;

	char *OSPFN_DEFAULT_CONFIG_FILE;
	char *OSPFN_LOCAL_HOST;
};

struct ospfn *ospfn;
struct thread_master *master;

#define CCN_NAME_FORMAT_URI 0
#define CCN_NAME_FORMAT_CCNB 1

void process_command_ccnname(char *command);
void process_command_logdir(char *command);
void process_command_ccnnametype(char *command);
void process_command_multipath_order(char *command);
void process_conf_command(char *command, int isLogOnlyProcessing);
void init_ospfn(void);
int readConfigFile(const char *filename , int isLogOnlyProcessing);
void add_nexthop(struct in_addr address, unsigned int pref_order);
int no_nexthop(void);
void display_nexthop(void);
struct in_addr pop_nexthop_from_position (unsigned int pos);
void pop_nexthop(void);
void free_all_nexthop( void );
int is_nexthop(char *address);
void inject_name_opaque_lsa( struct name_prefix *np, unsigned int op_id);
void init(void);
void ospfn_stop_signal_handler(int sig);
void pid_create(pid_t pid);
#endif
