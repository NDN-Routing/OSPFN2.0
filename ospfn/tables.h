#ifndef _TABLES_H_
#define _TABLES_H_

#include<zebra.h>
#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>

#include "hash.h"
#include "memory.h"
#include "memtypes.h"

#include "lib/prefix.h"
#include "ospfd/ospfd.h"
#include "ospfd/ospf_asbr.h"
#include "ospfd/ospf_lsa.h"
#include "ospfclient/ospf_apiclient.h"

#include <ccn/ccn.h>

struct ccn *ccn_handle;

struct name_prefix
{
	unsigned int limit;
	unsigned int length;
	u_char *name;
};

struct nameprefix_entry
{
	struct name_prefix *nameprefix;
	struct nameprefix_entry *next;
};

struct nexthop_entry
{
	struct in_addr nexthop;
	unsigned int cost;
	unsigned int flag;
	struct nexthop_entry *next;
};

struct origin_entry
{
	struct in_addr origin;
	struct origin_entry *next;
};

struct prefixtable_entry
{
	struct name_prefix *nameprefix;
	struct origin_entry *origin_list;
	struct nexthop_entry *nexthop_list;
};

struct origintable_entry
{
	struct in_addr origin;
	struct nameprefix_entry *nameprefix_list;
	struct nexthop_entry *nexthop_list;
};

void hash_iterate_delete_npt (struct hash *hash);
extern struct hash *origin_hash_create(void);
extern struct origintable_entry *origin_hash_get(struct hash *hash, struct in_addr *router_id);
extern void update_origin_nexthop_list(struct origintable_entry *oe, int nexthop_count, struct in_addr *nexthops);

extern struct hash *prefix_hash_create(void);
extern struct prefixtable_entry *prefix_hash_get(struct hash *hash, struct name_prefix *nameprefix);
extern void update_name_prefix_nexthop_list(struct prefixtable_entry *fe, struct hash *origin_table);

extern void add_new_name_prefix(struct ospf_apiclient *oclient, struct hash *prefix_table, struct name_prefix *np,
struct hash *origin_table, struct in_addr *origin);
extern void delete_name_prefix(struct hash *prefix_table, struct name_prefix *np, struct hash *origin_table, struct in_addr *origin);

#endif
