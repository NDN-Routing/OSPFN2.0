/**
* @file utility.c utility for ospfn
*
* Contains Different Utility Function.
*
* Author: A K M Mahmudul Hoque
*
*/
#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>
#include<stdarg.h>

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "utility.h"
#include "str.h"



/**
*
* align character to 4 bytes by padding character at end of string 
*
*/

u_char * 
align_data(u_char *data, unsigned int mod)
{
	unsigned int i;	
	unsigned int len;
	if (strlen((char *)data)%4 == mod)
		return data;
	else
	{
		len=strlen((char *)data);
		for(i=len; i< (((len+3)/4)*4+mod);i++)
			data[i]='|'; 
	}
	data[i]='\0';

	return data;
}

/**
*
*return lowercase of a string
*
*/

char * 
strToLower(char *str)
{
	int i;
	for(i=0; str[i]; i++)
		str[i]=tolower(str[i]);

	return str;
}



/**
*
*return substring of a string
*
*/

char * 
substring(const char* str, size_t begin, size_t len) 
{ 
	if (str == 0 || strlen(str) == 0 || strlen(str) < begin || strlen(str) < (begin+len)) 
		return 0; 

  	return strndup(str + begin, len); 
}

/**
*
* return current localtime from the system
*
*/

char * 
getLocalTimeStamp(void)
{
	char *timestamp = (char *)calloc(17,sizeof(char));
	time_t ltime;
	ltime=time(NULL);
	struct tm *tm;
	tm=localtime(&ltime);  
	sprintf(timestamp, "%04d%02d%02d%02d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, 
		tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	memcpy(timestamp+16,"\0",1);	
	return timestamp;
}

/**
*
*Check whether the loggingDir exists Otherwise creates LoggingDir 
*in default location and creates log file in it and return it.
*
*/

char * 
startLogging(char *loggingDir)
{
	struct passwd pd;
	struct passwd* pwdptr=&pd;
	struct passwd* tempPwdPtr;
	char *pwdbuffer;
	int  pwdlinelen = 200;
	char *logDir;
	char *logFileName;
	char *ret;
	char *logExt;
	char *defaultLogDir;	
	int status;
	struct stat st;
	int isLogDirExists=0;
	char *time=getLocalTimeStamp();

	pwdbuffer=(char *)calloc(200,sizeof(char));		
	logDir=(char *)calloc(200,sizeof(char));
	logFileName=(char *)calloc(200,sizeof(char));	
	logExt=(char *)calloc(5,sizeof(char));
	defaultLogDir=(char *)calloc(10,sizeof(char));

	memcpy(logExt,".log",4);
	logExt[4]='\0';
	memcpy(defaultLogDir,"/ospfnLog",9);
	defaultLogDir[9]='\0';
	
	if(loggingDir!=NULL)
 	{
		if( stat( loggingDir, &st)==0)
		{
			if ( st.st_mode & S_IFDIR )
			{
				if( st.st_mode & S_IWUSR)
				{
					isLogDirExists=1;
					memcpy(logDir,loggingDir,strlen(loggingDir)+1);
				}
				else printf("User do not have write permission to %s \n",loggingDir);
			}
			else printf("Provided path for %s is not a directory!!\n",loggingDir);
    		}
  		else printf("Log directory: %s does not exists\n",loggingDir);
	} 
  
	if(isLogDirExists == 0)
  	{
		if ((getpwuid_r(getuid(),pwdptr,pwdbuffer,pwdlinelen,&tempPwdPtr))!=0)
     			perror("getpwuid_r() error.");
  		else
  		{
			memcpy(logDir,pd.pw_dir,strlen(pd.pw_dir)+1);	
			memcpy(logDir+strlen(logDir),defaultLogDir,strlen(defaultLogDir)+1);	
			if(stat(logDir,&st) != 0)
				status = mkdir(logDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);	
		}
	}	
 	memcpy(logFileName,logDir,strlen(logDir)+1);	
	if( logDir[strlen(logDir)-1]!='/')
	{
		memcpy(logFileName+strlen(logFileName),"/",1);
		memcpy(logFileName+strlen(logFileName),"\0",1);	
	}	
	memcpy(logFileName+strlen(logFileName),time,strlen(time)+1);	
	memcpy(logFileName+strlen(logFileName),logExt,strlen(logExt)+1);	
	ret=(char *)calloc(strlen(logFileName)+1,sizeof(char));
    memcpy(ret,logFileName,strlen(logFileName)+1); 
	free(time);	
	free(logDir);
	free(logFileName);	
	free(pwdbuffer);	
	free(logExt);
	free(defaultLogDir);	
	return ret;	
}

/**
*
* write log to logfile
*
*/

void 
writeLogg(const char  *file, const char *source_file, const char *function, const int line, const char *format, ...)
{
	if (file != NULL)
	{
		FILE *fp = fopen(file, "a");
		
		if (fp != NULL)
		{            
			struct timeval t;
			gettimeofday(&t, NULL);
			fprintf(fp,"%ld.%06u - %s, %s, %d:",(long)t.tv_sec , (unsigned)t.tv_usec , source_file , function , line);        
			va_list args;
			va_start(args, format);
			vfprintf(fp, format, args);
			fclose(fp);
			va_end(args);	
		}
    	}
}

