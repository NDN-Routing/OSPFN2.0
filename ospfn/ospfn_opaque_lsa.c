/**
* @file ospfn_opaque_lsa.c
* 
* Handle opaque lsa's functionality
*
* @author A K M Mahmudul Hoque
*
*/

#include <zebra.h>
#include "prefix.h" /* needed by ospf_asbr.h */
#include "privs.h"
#include "log.h"
//header added for ospfn implementation
#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>

#include "ospfn_opaque_lsa.h"
#include "utility.h"
#include "ospfn.h"
#include "lib/table.h"

#include "ospfd/ospf_api.h"
#include "ospfd/ospf_dump.h"
#include "ospfclient/ospf_apiclient.h"
#include "lib/stream.h"



/**
*
* print opaque lsa content
*
*/

void 
ccnx_opaque_lsa_print (struct ccnx_opaque_lsa  *col)
{
	struct lsa_header *lsah = &col->lsah;
	int opaque_type=ntohl(lsah->id.s_addr)>>24;

	switch( opaque_type)
	{
		case NAME_OPAQUE_LSA:
			ccnx_name_opaque_lsa_print((struct ccnx_opaque_lsa *)col);
			break;
		case ADJ_OPAQUE_LSA:
			break;
 	}
}

/**
*
* print name OLSA content
*
*/

void 
ccnx_name_opaque_lsa_print (struct ccnx_opaque_lsa  *ol)
{
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"  Name Opaque LSA\n"); 
	struct name_opaque_lsa *nol=(struct name_opaque_lsa *)ol;
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Name Prefix: %s\n",(char *)nol->body.name_data); 
}


/**
*
* print LSA header information
*
*/

void 
ccnx_lsa_header_dump (struct lsa_header *lsah)
 {
   	const char *lsah_type = LOOKUP (ospf_lsa_type_msg, lsah->type);
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"  LSA Header\n");
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    LS age %d\n", ntohs (lsah->ls_age));
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Options %d (%s)\n", lsah->options, ospf_options_dump (lsah->options));   
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    LS type %d (%s)\n", lsah->type, (lsah->type ? lsah_type : "unknown type"));
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Link State ID %s\n", inet_ntoa (lsah->id)); 
	if( lsah->type == 9 || lsah->type == 10 || lsah->type ==11)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Opaque Type  %d\n" ,ntohl(lsah->id.s_addr)>>24);
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Opaque Id  %d\n" ,ntohl(lsah->id.s_addr)& LSID_OPAQUE_ID_MASK);
	}
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Advertising Router %s\n", inet_ntoa (lsah->adv_router));
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    LS sequence number 0x%lx\n", (u_long)ntohl (lsah->ls_seqnum));
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    LS checksum 0x%x\n", ntohs (lsah->checksum));
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    length %d\n", ntohs (lsah->length)); 
}

/**
*
* print out router LSA
*
*/

void 
ospf_router_lsa_print (struct router_lsa  *rl)
{
   	int i, len;
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, "  Router-LSA\n");
   	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    # links %d\n", ntohs (rl->links)); 
   	len = ntohs (rl->header.length) - OSPF_LSA_HEADER_SIZE - 4;
   	for (i = 0; len > 0; i++)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Link ID %s\n", inet_ntoa (rl->link[i].link_id));
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Link Data %s\n", inet_ntoa (rl->link[i].link_data));
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    Type %d\n", (u_char) rl->link[i].type);
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    TOS %d\n", (u_char) rl->link[i].tos);
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"    metric %d\n", ntohs (rl->link[i].metric));

		len -= 12;
	}
 }


/**
*
* Updates are carried out for OLSA
*
*/

void 
update_opaque_lsa(struct ccnx_opaque_lsa *col)
{
	struct lsa_header *lsah = &col->lsah;
	int opaque_type=ntohl(lsah->id.s_addr)>>24;

	switch( opaque_type)
	{
		case NAME_OPAQUE_LSA:
			update_name_opaque_lsa((struct ccnx_opaque_lsa *) col);
			break;
		case ADJ_OPAQUE_LSA:
			break;  
	}
}


/**
*
* update name prefix table with name prefix from Name OLSA
*
*/

void 
update_name_opaque_lsa(struct ccnx_opaque_lsa *col)
{

    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, "  Update_name_opaque_lsa called \n");
    	struct name_prefix *np=(struct name_prefix *)malloc(sizeof(struct name_prefix));
	struct name_opaque_lsa *nol=(struct name_opaque_lsa *)col;
	np->length=nol->body.name_length;
	np->name=nol->body.name_data;	
	np->name[np->length-1]='\0';		
	add_new_name_prefix(ospfn->oclient, ospfn->prefix_table, np, ospfn->origin_table, &col->lsah.adv_router);
	free(np);
}


/**
*
* delete operation for OLSA is carried out 
*
*/

void 
delete_opaque_lsa(struct ccnx_opaque_lsa *col)
{

	struct lsa_header *lsah = &col->lsah;
	int opaque_type=ntohl(lsah->id.s_addr)>>24;

	switch( opaque_type)
	{
		case NAME_OPAQUE_LSA:
			delete_name_opaque_lsa((struct ccnx_opaque_lsa *) col);
			break;
		case ADJ_OPAQUE_LSA:
			break;
	}
}

/**
*
* delete name prefix from name prefix table with name prefix from Name OLSA
*
*/

void 
delete_name_opaque_lsa(struct ccnx_opaque_lsa * col)
{

	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, " Delete _name opaque lsa called \n");
	struct name_prefix *np=(struct name_prefix *)malloc(sizeof(struct name_prefix));
    	struct name_opaque_lsa *nol=(struct name_opaque_lsa *)col;	
	np->length=nol->body.name_length;
	np->name=nol->body.name_data;
	np->name[np->length-1]='\0';		
	delete_name_prefix(ospfn->prefix_table, np, ospfn->origin_table, &col->lsah.adv_router);
}
