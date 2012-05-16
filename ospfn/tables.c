/*
* 
* @file tables.c 
* 
* Name prefix table
*
* @author Cheng Yi
*
*/

#include "tables.h"
#include "jhash.h"
#include "ccn_fib.h"
#include "ospfn.h"
#include "utility.h"

/**
*
* delete ccnd fib entry for every name prefix entry in name prefix table
*
*/

void 
hash_iterate_delete_npt (struct hash *hash){
   	unsigned int i,j,no_element;
   	struct hash_backet *hb;
   	struct hash_backet *hbnext;
	no_element=no_nexthop();	
 
   	for (i = 0; i < hash->size; i++)
	{
    		for (hb = hash->index[i]; hb; hb = hbnext)
		{
			struct prefixtable_entry *pte = (struct prefixtable_entry *)hb->data;
			struct nexthop_entry *n1 = pte->nexthop_list; // get the pointer of next hop list
			
			while(n1)
			{
				writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Deleting FIB prefix: Name: %s, Next Hop: %s, cost: %d, flag: %d\n",pte->nameprefix->name, inet_ntoa(n1->nexthop),n1->cost, n1->flag);
				if(!is_nexthop(inet_ntoa(n1->nexthop)))	
					delete_ccn_face(ccn_handle, (char *)pte->nameprefix->name, inet_ntoa(n1->nexthop), 9695); 
				n1 = n1->next;
			}
			for(j=1;j<=no_element;j++)
				delete_ccn_face(ccn_handle, (char *)pte->nameprefix->name, inet_ntoa(pop_nexthop_from_position(j)),9695);
	
			hbnext = hb->next;
		}
	}
 }


/* ------------ origin table functions ------------ */

/*
* p: in_addr (key)
*
*/
static unsigned int 
origin_key_make(void *p)
{
	struct in_addr *router_id = (struct in_addr *)p;
	unsigned int key = jhash(router_id, sizeof(struct in_addr), 314159);
	return key;
}

/**
*
* p1: origintable_entry (data); p2: in_addr (key)
*
*/
static int 
origin_cmp(const void *p1, const void *p2)
{
	const struct origintable_entry *oe = (const struct origintable_entry *)p1;
	const struct in_addr *id = (const struct in_addr *)p2;

	return (oe->origin.s_addr == id->s_addr);
}                                                         


/**
*
* returns origintable_entry (data)
*
*/

//p: in_addr (key); returns origintable_entry (data)
static void *
origin_hash_alloc(void *p)
{
	struct in_addr *router_id = (struct in_addr *)p;
	struct origintable_entry *oe;

	oe = malloc(sizeof(struct origintable_entry));
	oe->origin.s_addr = router_id->s_addr;
	oe->nameprefix_list = NULL;
	oe->nexthop_list = NULL;

	return oe;
}

struct hash *
origin_hash_create()
{
	struct hash *origin_hash = hash_create(origin_key_make, origin_cmp);
	return origin_hash;
}

struct origintable_entry *
origin_hash_get(struct hash *hash, struct in_addr *router_id)
{
	struct origintable_entry *oe = hash_get(hash, router_id, origin_hash_alloc);
	return oe;
}

/**
*
* returns number of items inserted: 0 if item already exists, 1 otherwise
*
*/

static int 
insert_nexthop_to_list(struct nexthop_entry **nexthop_list, struct in_addr nexthop,
        unsigned int cost, unsigned int flag)
{
	struct nexthop_entry *nhe, *prev, *cur;
	int result = 1;

	nhe = malloc(sizeof(struct nexthop_entry));
	nhe->nexthop.s_addr = nexthop.s_addr;
	nhe->cost = cost;
	nhe->flag = flag;

	if (*nexthop_list == NULL)
	{
		nhe->next = NULL;
		*nexthop_list = nhe;
	}
	else
	{
		prev = NULL;
		cur = *nexthop_list;

		while (cur && cur->nexthop.s_addr < nexthop.s_addr)
		{
			prev = cur;
			cur = cur->next;
		}
		//nexthop already exists
		if (cur && cur->nexthop.s_addr == nexthop.s_addr)
		{
			result = 0;
			free(nhe);
		}
		else
		{
			if (prev == NULL)
			{
				*nexthop_list = nhe;
				nhe->next = cur;
			}
			else
			{
				prev->next = nhe;
				nhe->next = cur;
			}
		}
	}

	return result;
}

/**
*returns number of items inserted: 0 if item already exists, 1 otherwise
*/

static int 
insert_origin_to_list(struct origin_entry **origin_list, struct in_addr *origin)
{
	struct origin_entry *oe, *prev, *cur;
	int result = 1;

	oe = malloc(sizeof(struct origin_entry));
	oe->origin.s_addr = origin->s_addr;

	if (*origin_list == NULL)
	{
		oe->next = NULL;
		*origin_list = oe;
	}
	else
	{
		prev = NULL;
		cur = *origin_list;

		while (cur && cur->origin.s_addr < origin->s_addr)
		{
			prev = cur;
			cur = cur->next;
		}

		//nexthop already exists
		if (cur && cur->origin.s_addr == origin->s_addr)
		{
			result = 0;
			free(oe);
		}
		else
		{
			if (prev == NULL)
			{
				*origin_list = oe;
				oe->next = cur;
			}
			else
			{
				prev->next = oe;
				oe->next = cur;
			}
		}
	}

	return result;
}

/**
*returns number of items deleted: 0 if item does not exist, 1 otherwise
*/

static int 
delete_origin_from_list(struct origin_entry **origin_list, struct in_addr *origin)
{
	struct origin_entry *prev, *cur;
	int result = 1;

	prev = NULL;
	cur = *origin_list;

	while (cur && cur->origin.s_addr < origin->s_addr)
	{
		prev = cur;
		cur = cur->next;
	}

	//found
	if (cur && cur->origin.s_addr == origin->s_addr)
	{
		if (prev == NULL)
			*origin_list = cur->next;
		else
			prev->next = cur->next;

		free(cur);
	}
	else
		result = 0;

	return result;
}

/**
*
*returns number of items inserted: 0 if item already exists, 1 otherwise
*/

static int 
insert_name_prefix_to_list(struct nameprefix_entry **nameprefix_list, struct name_prefix *np)
{
	struct nameprefix_entry *ne, *prev, *cur;
	int result = 1;

	ne = malloc(sizeof(struct nameprefix_entry));
	ne->nameprefix = malloc(sizeof(struct name_prefix));
	ne->nameprefix->limit = np->limit;
	ne->nameprefix->length = np->length;
	ne->nameprefix->name = (u_char *)strdup((char *)np->name);

	if (*nameprefix_list == NULL)
	{
		ne->next = NULL;
		*nameprefix_list = ne;
	}
	else
	{
		prev = NULL;
		cur = *nameprefix_list;

		while (cur && strcmp((char *)cur->nameprefix->name, (char *)np->name) < 0)
		{
			prev = cur;
			cur = cur->next;
		}

		//nexthop already exists
		if (cur && strcmp((char *)cur->nameprefix->name, (char *)np->name) == 0)
		{
			result = 0;
			free(ne->nameprefix->name);
 			free(ne->nameprefix);
			free(ne);
		}
		else
		{
			if (prev == NULL)
			{
				*nameprefix_list = ne;
				ne->next = cur;
			}
			else
			{
				prev->next = ne;
				ne->next = cur;
			}
		}
	}

	return result;
}

/**
*returns number of items deleted: 0 if item does not exist, 1 otherwise
*/

static int 
delete_name_prefix_from_list(struct nameprefix_entry **nameprefix_list, struct name_prefix *np)
{
	struct nameprefix_entry *prev, *cur;
	int result = 1;

	prev = NULL;
	cur = *nameprefix_list;

	while (cur && strcmp((char *)cur->nameprefix->name, (char *)np->name) < 0)
	{
 		prev = cur;
		cur = cur->next;
	}

	//found
	if (cur && strcmp((char *)cur->nameprefix->name, (char *)np->name) == 0)
	{
		if (prev == NULL)
			*nameprefix_list = cur->next;
		else
			prev->next = cur->next;

		free(cur->nameprefix->name);
		free(cur->nameprefix);
		free(cur);
	}
	else
		result = 0;

	return result;
}

void 
update_origin_nexthop_list(struct origintable_entry *oe, int nexthop_count, struct in_addr *nexthops)
{
	struct nexthop_entry *head = oe->nexthop_list;
	int i;

	//remove old nexthops
	while (head)
	{
		oe->nexthop_list = head->next;
		free(head);
		head = oe->nexthop_list;
	}

	assert(oe->nexthop_list == NULL);

	//add new nexthops
	for (i = 0; i < nexthop_count; i ++)
		insert_nexthop_to_list(&oe->nexthop_list, nexthops[i], 0, 0);
}

/* ------------ prefix table functions ------------ */

//p: name_prefix (key)
static unsigned int 
prefix_key_make(void *p)
{
	struct name_prefix *np = (struct name_prefix *)p;
	unsigned int key = jhash(np->name, np->length, 314159);
	return key;
}

//p1: prefixtable_entry (data); p2: name_prefix (key)
static int 
prefix_compare(const void *p1, const void *p2)
{
	const struct prefixtable_entry *fe = (const struct prefixtable_entry *)p1;
	struct name_prefix *np1 = fe->nameprefix;
	const struct name_prefix *np2 = (const struct name_prefix *)p2;
	return (np1->length == np2->length && memcmp(np1->name, np2->name, np1->length) == 0);
}                                                         

/**
* returns prefixtable_entry
*/

//p: name_prefix (key); returns prefixtable_entry (data)
static void 
*prefix_hash_alloc(void *p)
{
	struct name_prefix *np = (struct name_prefix *)p;
	struct prefixtable_entry *fe;

	fe = malloc(sizeof(struct prefixtable_entry));
	fe->nameprefix = malloc(sizeof(struct name_prefix));
	fe->nameprefix->limit = np->limit;
	fe->nameprefix->length = np->length;
	fe->nameprefix->name = (u_char *)strdup((char *)np->name);

	fe->origin_list = NULL;
	fe->nexthop_list = NULL;

	return fe;
}

struct hash *
prefix_hash_create()
{
	struct hash *prefix_hash = hash_create(prefix_key_make, prefix_compare);
	return prefix_hash;
}

struct prefixtable_entry *
prefix_hash_get(struct hash *hash, struct name_prefix *nameprefix)
{
	struct prefixtable_entry *fe = hash_get(hash, nameprefix, prefix_hash_alloc);
	return fe;
}

void 
update_name_prefix_nexthop_list(struct prefixtable_entry *fe, struct hash *origin_table)
{
	struct nexthop_entry *nhe;
	struct origin_entry *o = fe->origin_list;
	struct origintable_entry *oe;
	int res,j, no_element;
	
	no_element=no_nexthop();	
	//printf("-name prefix: %s\n", fe->nameprefix->name);
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, "-name prefix: %s\n", fe->nameprefix->name);
	//remove old nexthops
	nhe = fe->nexthop_list;

	
	while (nhe)
	{
		if(!is_nexthop(inet_ntoa(nhe->nexthop)))	
			delete_ccn_face(ccn_handle, (char *)fe->nameprefix->name, inet_ntoa(nhe->nexthop), 9695);
		fe->nexthop_list = nhe->next;
		free(nhe);
		nhe = fe->nexthop_list;
	}

	for(j=1;j<=no_element;j++)
		delete_ccn_face(ccn_handle, (char *)fe->nameprefix->name, inet_ntoa(pop_nexthop_from_position(j)),9695);	
	
	//foreach origin in origin_list
	while (o)
	{
		//get origintable_entry
		oe = origin_hash_get(origin_table, &(o->origin));

		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"--origin: %s\n", inet_ntoa(oe->origin));
		nhe = oe->nexthop_list;

			
		//foreach nexthop
		while (nhe)
		{
			writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"---nexthop: %s\n", inet_ntoa(nhe->nexthop));
			res = insert_nexthop_to_list(&fe->nexthop_list, nhe->nexthop, nhe->cost, nhe->flag);

			if (res == 1)
			{	
				for(j=1;j<=no_element;j++)
				{		
					if(inet_addr(inet_ntoa(nhe->nexthop)) != inet_addr(inet_ntoa(pop_nexthop_from_position(j)))){		
					add_ccn_face(ccn_handle, (char *)fe->nameprefix->name, inet_ntoa(pop_nexthop_from_position(j)),9695);
					writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Adding face to ccn for prefix : %s nexthop : %s\n",(char *)fe->nameprefix->name, inet_ntoa(pop_nexthop_from_position(j)));	
					}
				}	
				add_ccn_face(ccn_handle, (char *)fe->nameprefix->name, inet_ntoa(nhe->nexthop), 9695);
				writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Adding face to ccn for prefix : %s nexthop : %s\n",(char *)fe->nameprefix->name, inet_ntoa(nhe->nexthop));
			}	
			nhe = nhe->next;
		}
		o = o->next;
	}
}

void 
add_new_name_prefix(struct ospf_apiclient *oclient, struct hash *prefix_table, struct name_prefix *np,
        struct hash *origin_table, struct in_addr *origin)
{
	struct prefixtable_entry *fe;
	struct origintable_entry *oe;
	int rc;

	fe = prefix_hash_get(prefix_table, np);
	insert_origin_to_list(&fe->origin_list, origin);

	oe = origin_hash_get(origin_table, origin);
	insert_name_prefix_to_list(&oe->nameprefix_list, np);

	if (oe->nexthop_list == NULL)
 	{	
		rc=ospf_apiclient_get_router_nexthops(oclient, *origin);
		//writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Return from nexthop query: %d\n",rc);	
		//writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Nexthop Null for origin: %s and prefix: %s\n",inet_ntoa(*origin),np->name);	
	}	
	else
		update_name_prefix_nexthop_list(fe, origin_table);
}

void 
delete_name_prefix(struct hash *prefix_table, struct name_prefix *np, struct hash *origin_table, struct in_addr *origin)
{
	struct prefixtable_entry *fe;
	struct origintable_entry *oe;
	int res;
	fe = prefix_hash_get(prefix_table, np);
	res = delete_origin_from_list(&fe->origin_list, origin);
	if (res == 0)
	{
		printf("Error: origin %s does not exist\n", inet_ntoa(*origin));
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Error: origin %s does not exist\n",inet_ntoa(*origin)); 
	}
	oe = origin_hash_get(origin_table, origin);
	res = delete_name_prefix_from_list(&oe->nameprefix_list, np);
	if (res == 0)
	{
		printf("Error: name prefix %s does not exist\n",(char *) np->name);
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Error: name prefix %s does not exist\n",(char *) np->name); 
	}
	update_name_prefix_nexthop_list(fe, origin_table);
}
