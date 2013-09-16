/*
Header file utility.h
Definition is provided in utility.c
*/
#ifndef _UTILITY_H_
#define _UTILITY_H_

u_char * align_data(u_char *data, unsigned int mod);
char * strToLower(char *str);
char * substring(const char* str, size_t begin, size_t len);
char * getLocalTimeStamp(void);
char * startLogging(char *loggingDir);
void writeLogg(const char  *file,  const char *source_file, const char *function, const int line, const char *format, ...);

#endif
