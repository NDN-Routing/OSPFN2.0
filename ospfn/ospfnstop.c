/* 
 * @file ospfnstop.c
 *
 * ospfnstop -- utility for stopping ospfn 
 * 
 * @author A K M Mahmudul Hoque
 */

#include <zebra.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int 
main(){
	FILE *fp;
	int pid;	
	fp=fopen(PATH_OSPFN_PID,"r");
	if(fp!=NULL)
	{
		fscanf(fp,"%d",&pid);
		kill(pid, SIGTERM);	
	}
	else 
	  	fprintf(stderr, "ospfnstop: No process running or user does not have permission to open the pid file\n");
	return 0;
}



