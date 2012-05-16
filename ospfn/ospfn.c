/**
*
* @mainpage OSPFN A Dynamic CCND FIB manipulation
*
* For building and Installation
* @see README
*
*/

/**
 * @file ospfn.c
 * 
 * OSPFN program designed for dynamic FIB manipulation for CCND
 *
 * @author A K M Mahmudul Hoque
 *
 */

/* The following includes are needed in all OSPF API client
   applications. */
#include <zebra.h>
#include "prefix.h" /* needed by ospf_asbr.h */
#include "privs.h"
#include "log.h"
/* header added for ospfn implementation */
#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>
#include<signal.h>
/* header for making daemon process */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

/*header for ospfnstop response */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/un.h>

#include "getopt.h"
#include "ospfd/ospfd.h"
#include "ospfd/ospf_asbr.h"
#include "ospfd/ospf_lsa.h"
#include "lib/table.h"

#include "ospfn_opaque_lsa.h"
#include "utility.h"
#include "ospfn.h"

#include "ospfd/ospf_opaque.h"
#include "ospfd/ospf_api.h"
#include "ospfclient/ospf_apiclient.h"
#include "lib/stream.h"
#include "ospfd/ospf_zebra.h"

#include <ccn/ccn.h>


/* privileges struct. 
 * set cap_num_* and uid/gid to nothing to use NULL privs
 * as ospfapiclient links in libospf.a which uses privs.
 */
struct zebra_privs_t ospfd_privs =
{
    .user = NULL,
    .group = NULL,
    .cap_num_p = 0,
    .cap_num_i = 0
};

struct option longopts[] =
{
    { "daemon",      no_argument,       NULL, 'd'},
    { "config_file", required_argument, NULL, 'f'},
    { "help",        no_argument,       NULL, 'h'},
    { "log",         no_argument,       NULL, 'n'},
    { 0 }
};    


/* The following includes are specific to this application. For
   example it uses threads from libzebra, however your application is
   free to use any thread library (like pthreads). */

#include "ospfd/ospf_dump.h" /* for ospf_lsa_header_dump */
#include "thread.h"
#include "log.h"




/*------------------------------------------*/
/*    FUNCTION DEFINITON                    */
/*------------------------------------------*/

/**
* 
*initialize signal handler for ospfn program
*
*/

void 
init(void)
{
	if (signal(SIGQUIT, ospfn_stop_signal_handler ) == SIG_ERR) 
	{
		perror("SIGQUIT install error\n");
		exit(1);
	}
	if (signal(SIGTERM, ospfn_stop_signal_handler ) == SIG_ERR) 
	{
		perror("SIGTERM install error\n");
		exit(1);
    	}
 	if (signal(SIGINT, ospfn_stop_signal_handler ) == SIG_ERR)
	{
		perror("SIGTERM install error\n");
		exit(1);
	}	
	ospfn->CCN_NAME_TYPE=CCN_NAME_FORMAT_URI;
	ospfn->mp=NULL;
}

/**
*
* ospfn program's signal handler function
*
*/

void 
ospfn_stop_signal_handler(int sig)
{
	signal(sig, SIG_IGN);
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Signal for ospfn stop\n");
	hash_iterate_delete_npt (ospfn->prefix_table);
    	ccn_destroy(&ccn_handle);	
    	ospf_apiclient_close(ospfn->oclient);	
	free_all_nexthop();	
	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Exiting ospfn...\n");	
	exit(0);
}

/**
*
* pid file creation function
*
*/

void pid_create(pid_t pid)
{
	FILE *fp;
	fp=fopen(PATH_OSPFN_PID,"w");
	if(fp!=NULL)
	{
		fprintf(fp,"%d\n",pid+1);
		fclose(fp);	
	}
	else 
	{
	 	fprintf (stderr, "pid create: can not create pid file in %s directory\n", PATH_OSPFN_PID);	
		exit(1);
	}	
}


/* ------------- Function Added for Ospfn Implementation ------------------------ */

/**
*
* print usage information 
*
*/

static int 
usage(char *progname)
{

    printf("Usage: %s [OPTIONS...]\n\
	Announces name prefix LSAs and modifies ccnd FIB.\n\n\
	-d, --daemon        Run in daemon mode\n\
	-f, --config_file   Specify configuration file name\n\
	-h, --help          Display this help message\n", progname);

    exit(1);
}


/**
*
* process ccnname commands and call for name OLS injection
*
*/
void 
process_command_ccnname(char *command)
{
	if(command==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id]\n");
		return;
	}
	
	char *rem;
	const char *sep=" \t\n";
	char *name;	
	char *opId;
	unsigned int op_id;
	struct name_prefix *np=(struct name_prefix *)malloc(sizeof(struct name_prefix));

	/* procesing of ccn name commands here */
	name=strtok_r(command,sep,&rem);
	if(name==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id\n");
		return;
 	}
	
	opId=strtok_r(NULL,sep,&rem);
	if(opId==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id\n");
		return;
	}
	op_id=atoi(opId);
	np->length=strlen(name)+1;
	np->name=(u_char *)name;	
	np->name[np->length]='\0';
	inject_name_opaque_lsa(np,op_id);	
 	free(np);
}

/**
*
* process logdir command and set the logdirectory
*
*/

void 
process_command_logdir(char *command)
{
	if(command==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Wrong Command Format ( logdir /path/to/logdir )\n");
		return;
	}
	char *rem;
	const char *sep=" \t\n";
	char *dir;

	dir=strtok_r(command,sep,&rem);
	if(dir==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Wrong Command Format ( logdir /path/to/logdir/ )\n");
		return;
	}
	ospfn->loggingDir=strdup(dir);
}

/**
*
* process ccnnametype command and set nametype uri/ccnb
*
*/

void
process_command_ccnnametype(char *command)
{
	if(command==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Wrong Command Format ( ccnnametype uri/ccnb\n");
		return;
	}
	char *rem;
	const char *sep=" \t\n";
	char *nametype;

	nametype=strtok_r(command,sep,&rem);
	if(nametype==NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Wrong Command Format ( ccnnametype uri/ccnb )\n");
		return;
	}
	if(!strcmp(strToLower(nametype),"uri"))
	{
		ospfn->CCN_NAME_TYPE=CCN_NAME_FORMAT_URI;
	}
	if(!strcmp(strToLower(nametype),"ccnb"))
	{
		ospfn->CCN_NAME_TYPE=CCN_NAME_FORMAT_CCNB;
	}
}


/**
*
* process multipath-order command 
*
*/

void
process_command_multipath_order(char *command)
{
	if(command==NULL)
        {
                writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id]\n");
                return;
        }

        char *rem;
        const char *sep=" \t\n";
        char *addr;    
        char *po;
       	struct in_addr address; 
	unsigned int pref_order;

        /* procesing of ccn name commands here */
        addr=strtok_r(command,sep,&rem);
        if(addr==NULL)
        {
                writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id\n");
                return;
        }
	inet_aton(addr,&address);

        po=strtok_r(NULL,sep,&rem);
        if(po==NULL)
        {
                writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong Command Format [ ccnname /name/prefix opaque_id\n");
                return;
        }
        pref_order=atoi(po);
	
	add_nexthop(address, pref_order);	
	display_nexthop();
}


/**
*
* process all configurations command and call each individual
* command processing sub routine
*
*/

void 
process_conf_command(char *command,int isLogOnlyProcessing)
{
	const char *separators=" \t\n";
	char *remainder=NULL;
	char *cmd_type=NULL;

	if(command==NULL || strlen(command)==0)
		return;	

	cmd_type=strtok_r(command,separators,&remainder);

	if(!strcmp(cmd_type,"ccnname") )
	{
		if (isLogOnlyProcessing == 0)
			process_command_ccnname(remainder);
	}
	else if(!strcmp(cmd_type,"ccnnametype") )
	{
		if (isLogOnlyProcessing == 0)
			process_command_ccnnametype(remainder);
	} 
	else if(!strcmp(cmd_type,"multipath-order") )
        {
                if (isLogOnlyProcessing == 0)
                        process_command_multipath_order(remainder);
	}
	else if(!strcmp(cmd_type,"logdir") )
	{
		if (isLogOnlyProcessing == 1)
			process_command_logdir(remainder);
	}
	else
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Wrong configuration command\n");
		printf("Wrong configuration Command %s \n",cmd_type);
	}
}

/**
*
* read configuration line by line and processes command from it
*
*/

int 
readConfigFile(const char *filename, int isLogOnlyProcessing)
{
	FILE *cfg;
	char buf[1024];
	int len;

	cfg=fopen(filename, "r");

	if(cfg == NULL)
	{
		printf("\nConfiguration File does not exists\n");
		exit(1);	
	}

	while(fgets((char *)buf, sizeof(buf), cfg))
	{
		len=strlen(buf);
		if(buf[len-1] == '\n')
		buf[len-1]='\0';		
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"%s\n",buf);
		process_conf_command(buf,isLogOnlyProcessing);	
	}

	fclose(cfg);

	return 0;
}

/**
*
* Add next hop in multipath order list
*
*/

void 
add_nexthop(struct in_addr address, unsigned int pref_order)
{
	struct multipath *current;	
	struct multipath *tmp = (struct multipath *)malloc(sizeof(struct multipath));
	tmp->nexthop.address=address;
	tmp->nexthop.pref_order=pref_order;

	if(ospfn->mp == NULL || pref_order < ospfn->mp->nexthop.pref_order)
	{
		tmp->next = ospfn->mp;
		ospfn->mp = tmp;	
	}
	else
	{
		current = ospfn->mp;
		
		while ( current->next != NULL && current->next->nexthop.pref_order <= pref_order)
			current = current->next;
		
		tmp->next = current->next;
		current->next = tmp;
	}
}	

/**
*
* Print Multipath order list
*
*/

void  
display_nexthop(void)
{
	struct multipath *current;

	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Display Queued Item: \n");

	current = ospfn->mp;
	if (  current == NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Empty Queue @display_nexthop \n");
	}
	else
	{
		while ( current != NULL)
		{
			writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Item: Address :%s   Pref_order:  %d \n",inet_ntoa(current->nexthop.address), current->nexthop.pref_order );
			current = current->next;
		}
	}
}

/**
* Return number of multipath configured 
*/

int 
no_nexthop(void)
{
	struct multipath *current;
	int count = 0;

	current = ospfn->mp;
	if (  current == NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Empty Queue @no_nexthop \n");
	}
	else
	{
		while ( current != NULL)
		{
			current = current->next;
			count++;
		}
	}

	return count;
}

/**
*
* Return nexthop from multipath from a position
*
*/

struct in_addr  
pop_nexthop_from_position (unsigned int pos)
{
	struct multipath *current;
	unsigned int count=1;
	if ( ospfn->mp == NULL )
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Empty Queue@pop_nexthop_from_position \n");	
	else
	{
		current = ospfn->mp;
		
		while ( current->next != NULL && count<pos)
		{
			count++;
			current = current->next;
		}

		if ( current != NULL)
			return current->nexthop.address;
	} 

	return;
}

/**
*
* Pop a nexthop from multipath 
*
*/

void 
pop_nexthop ( void )
{
	struct multipath *tmp;
	if ( ospfn->mp == NULL )
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Empty Queue@pop_nexthop \n");
	else
	{
		tmp = ospfn->mp;
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," Deleted item: Address :%s   Pref_order:  %d \n",inet_ntoa(tmp->nexthop.address), tmp->nexthop.pref_order );
		ospfn->mp = ospfn->mp->next;
		free ( tmp );
	} 
}

/**
*
* Free all allocated memory for multipath orders
*
*/

void 
free_all_nexthop( void )
{
	int i;
	int no_element=no_nexthop();

	for(i=1; i<=no_element; i++)
		pop_nexthop();

	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," All memory freed \n");
}

/**
*
* Check existence of address in multipath nexthop
*
*/

int    
is_nexthop(char *address)
{
	struct multipath *current;
	int exists=0;

	current = ospfn->mp;
	if (  current == NULL)
	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, " Empty Queue @ is_nexthop \n");
	}
	else
	{
		while ( current != NULL)
		{
			if(inet_addr(inet_ntoa(current->nexthop.address)) == inet_addr(address))	
				exists=1;			
			current = current->next;
		}
	}

	return exists;
}


/* ---------------------------------------------------------
 * Threads for asynchronous messages and LSA update/delete 
 * ---------------------------------------------------------
 */

/**
* This thread handles asynchronous messages coming in from the OSPF
*  API server 
*/
static int 
lsa_read (struct thread *thread)
{
	struct ospf_apiclient *oclient;
    	int fd;
    	int ret;

    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"lsa_read called\n");
    	oclient = THREAD_ARG (thread);
    	fd = THREAD_FD (thread);

    	/* Handle asynchronous message */
    	ret = ospf_apiclient_handle_async (ospfn->oclient);
    	if (ret < 0) 
    	{
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__," OSPFD Connection closed, exiting...\n");
		exit(0);
    	}

    	/* Reschedule read thread */
    	thread_add_read (master, lsa_read, ospfn->oclient, fd);
    	return 0;
}

/* ---------------------------------------------------------
 * Callback functions for asynchronous events 
 * ---------------------------------------------------------
 */

/**
* LSA Update call back function 
*
*/

static void 
lsa_update_callback (struct in_addr ifaddr, struct in_addr area_id,
        u_char is_self_originated,
        struct lsa_header *lsa)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"lsa_update_callback: \n");
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"ifaddr: %s \n", inet_ntoa (ifaddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"area: %s\n", inet_ntoa (area_id));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"is_self_origin: %u\n", is_self_originated);
	/* It is important to note that lsa_header does indeed include the
       header and the LSA payload. To access the payload, first check
       the LSA type and then typecast lsa into the corresponding type,
       e.g.:

       if (lsa->type == OSPF_ROUTER_LSA) {
       struct router_lsa *rl = (struct router_lsa) lsa;
       ...
       u_int16_t links = rl->links;
       ...
       }
       */

    	ccnx_lsa_header_dump (lsa);

    	if(lsa->type == OSPF_ROUTER_LSA)
	{    
		ospf_router_lsa_print((struct router_lsa *) lsa, lsa->length);
    	} 
    
	if(lsa->type == 9 || lsa->type == 10 || lsa->type == 11)
	{
		update_opaque_lsa((struct ccnx_opaque_lsa *) lsa);
		ccnx_opaque_lsa_print((struct ccnx_opaque_lsa *) lsa);
    	}
}

/**
* LSA Delete call back function 
*
*/

static void 
lsa_delete_callback (struct in_addr ifaddr, struct in_addr area_id,
        u_char is_self_originated,
        struct lsa_header *lsa)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"lsa_delete_callback: \n");
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"ifaddr: %s \n", inet_ntoa (ifaddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"area: %s\n", inet_ntoa (area_id));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"is_self_origin: %u\n", is_self_originated);
	
    	ccnx_lsa_header_dump (lsa);

    	if(lsa->type == 9 || lsa->type == 10 || lsa->type == 11)
	{
		delete_opaque_lsa((struct ccnx_opaque_lsa *) lsa);
    	}
}

/**
* LSA Ready call back function 
*
*/


static void 
ready_callback (u_char lsa_type, u_char opaque_type, struct in_addr addr)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"ready_callback: lsa_type: %d opaque_type: %d addr=%s\n",lsa_type, opaque_type, inet_ntoa (addr));
    	
	/* Schedule opaque LSA originate in 5 secs */
    	//thread_add_timer (master, lsa_inject, oclient, 5);

    	/* Schedule opaque LSA update with new value */
    	//thread_add_timer (master, lsa_inject, oclient, 10);

    	/* Schedule delete */
    	//thread_add_timer (master, lsa_delete, oclient, 30);
}

/**
* new interface call back function 
*
*/

static void 
new_if_callback (struct in_addr ifaddr, struct in_addr area_id)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"new_if_callback: ifaddr: %s \n", inet_ntoa (ifaddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"area_id: %s\n", inet_ntoa (area_id));
}

/**
* Interface Delete call back function 
*
*/

static void 
del_if_callback (struct in_addr ifaddr)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"new_if_callback: ifaddr: %s\n ", inet_ntoa (ifaddr));
}

/**
* Interface state machine change call back function 
*
*/

static void 
ism_change_callback (struct in_addr ifaddr, struct in_addr area_id,
        u_char state)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"ism_change: ifaddr: %s \n", inet_ntoa (ifaddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"area_id: %s\n", inet_ntoa (area_id));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"state: %d [%s]\n", state, LOOKUP (ospf_ism_state_msg, state));
}

/**
* Neighbor State Machine change call back function 
*
*/

static void 
nsm_change_callback (struct in_addr ifaddr, struct in_addr nbraddr,
        struct in_addr router_id, u_char state)
{
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"nsm_change: ifaddr: %s \n", inet_ntoa (ifaddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"nbraddr: %s\n", inet_ntoa (nbraddr));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"router_id: %s\n", inet_ntoa (router_id));
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"state: %d [%s]\n", state, LOOKUP (ospf_nsm_state_msg, state));
}

/* 08/29/2011 yic+ callback function for router routing table change */

/**
* Nexthop Change call back function 
*
*/

static void 
nexthop_change_callback(struct in_addr router_id, int nexthop_count, struct in_addr *nexthops)
{
    	struct origintable_entry *oe;
    	struct nameprefix_entry *ne;
    	struct prefixtable_entry *fe;

	//writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Next Hop Change Callback Called \n");

    	oe = origin_hash_get(ospfn->origin_table, &router_id);

    	update_origin_nexthop_list(oe, nexthop_count, nexthops);

    	ne = oe->nameprefix_list;
    	while (ne)
	{
		fe = prefix_hash_get(ospfn->prefix_table, ne->nameprefix);
		update_name_prefix_nexthop_list(fe, ospfn->origin_table);
		ne = ne->next;
    	}
}


/**
*
* inject Name OLSA in ospfd
*
*/

void 
inject_name_opaque_lsa(struct name_prefix *np, unsigned int op_id )
{
    	struct ospf_apiclient *cl;
    	struct in_addr ifaddr;
    	struct in_addr area_id;
    	u_char lsa_type;
    	u_char opaque_type;
    	int rc;

    	cl=ospfn->oclient;
    	inet_aton ("127.0.0.1", &ifaddr);    
	inet_aton ("0", &area_id);
    	lsa_type = 10;
    	opaque_type = 236;

    	struct name_opaque_body *nob= (struct name_opaque_body *)malloc( sizeof(struct name_opaque_body) + np->length);
	nob->name_length=np->length;
    	nob->name_type=ospfn->CCN_NAME_TYPE;
	np->name=align_data(np->name,0);
    	memcpy(nob->name_data,np->name,np->length);
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Originating/updating Name Opaque LSA... ");
    	/*injecting name opaque lsa here */
    	rc = ospf_apiclient_lsa_originate(cl, ifaddr, area_id,
            lsa_type,
            opaque_type, op_id,
            (void *)nob, sizeof(struct name_opaque_body) + np->length  );

    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Done, return code is %d\n", rc);

	free(nob);

}

/**
*
* Initialize ospfn
*
*/

void
init_ospfn(void)
{

	ospfn=(struct ospfn *)malloc(sizeof(struct ospfn));

	ospfn->OSPFN_DEFAULT_CONFIG_FILE=(char *)malloc(strlen("ospfn.conf")+1);
	ospfn->OSPFN_LOCAL_HOST=(char *)malloc(strlen("127.0.0.1")+1);	
	memcpy(ospfn->OSPFN_DEFAULT_CONFIG_FILE,"ospfn.conf",10);
	memcpy(ospfn->OSPFN_LOCAL_HOST,"127.0.0.1",9);

        ospfn->origin_table = origin_hash_create();
        ospfn->prefix_table = prefix_hash_create();
}

 
/* ---------------------------------------------------------
 * Main program 
 * ---------------------------------------------------------
 */

int 
main(int argc, char *argv[])
{	
    	struct thread main_thread;
    	int res;
    	int daemon_mode = 0;
    	int isLoggingEnabled = 1;
    	char *config_file ;
    
	init_ospfn();	
	config_file=ospfn->OSPFN_DEFAULT_CONFIG_FILE;
	
	while ((res = getopt_long(argc, argv, "df:hn", longopts, 0)) != -1) 
	{
        	switch (res) 
		{
			case 'd':
				daemon_mode = 1;
				break;
			case 'f':
				config_file = optarg;
				break;
			case 'n':
				isLoggingEnabled = 0;
				break;
			case 'h':
			default:
				usage(argv[0]);
		}
    	}

    	/* Initialization */
    	zprivs_init (&ospfd_privs);
    	master = thread_master_create ();

    	init(); 
    	pid_create(getpid()); 
	readConfigFile(config_file,1);

    	if(isLoggingEnabled)
		ospfn->logFile=startLogging( ospfn->loggingDir );
    
    	ccn_handle = ccn_create();
    	res = ccn_connect(ccn_handle, NULL); 
    	if (res < 0) 
	{
		ccn_perror(ccn_handle, "Cannot connect to ccnd.");
		exit(1);
    	}

    	/* Open connection to OSPF daemon */
    	writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Connecting to OSPF daemon ............\n");
    	ospfn->oclient = ospf_apiclient_connect (ospfn->OSPFN_LOCAL_HOST, ASYNCPORT);
    	if (!ospfn->oclient)
	{
		printf ("Connecting to OSPF daemon on 127.0.0.1 failed!\n");
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__, "Connecting to OSPF daemon on host: 127.0.0.1 failed!!\n");
		exit (1);
    	}
	else 
		writeLogg(ospfn->logFile,__FILE__,__FUNCTION__,__LINE__,"Connection to OSPF established. \n");

    	/* Register callback functions. */
    	ospf_apiclient_register_callback (ospfn->oclient,
            ready_callback,
            new_if_callback,
            del_if_callback,
            ism_change_callback,
            nsm_change_callback,
            lsa_update_callback,
            lsa_delete_callback,
            nexthop_change_callback);

    	/* Register LSA type and opaque type. */
	// registering adjacency opaque lsa
    	ospf_apiclient_register_opaque_type (ospfn->oclient, 10, ADJ_OPAQUE_LSA); 
	// registering name opaque lsa
    	ospf_apiclient_register_opaque_type (ospfn->oclient, 10, NAME_OPAQUE_LSA); 

    	/* Synchronize database with OSPF daemon. */
    	ospf_apiclient_sync_lsdb (ospfn->oclient);

    	readConfigFile(config_file,0);	
    	thread_add_read (master, lsa_read, ospfn->oclient, ospfn->oclient->fd_async);
    
    	if (daemon_mode)
        	daemon(0, 0);

    	while (1)
	{
		thread_fetch (master, &main_thread);
		thread_call (&main_thread);
    	}

	return 0;		
}

