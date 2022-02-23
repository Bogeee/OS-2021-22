#ifndef _STDIO_H
#include <stdio.h> /* fprintf, printf */
#endif
#ifndef _STDLIB_H
#include <stdlib.h>
#endif
#ifndef _STRING_H
#include <string.h> /* strerror */
#endif

#define _BASHPRINT_H

/* ANSI Color Standard */
/* Docs -> https://talyian.github.io/ansicolors/ */
#define COLOR_RED      "\x1b[31m"
#define COLOR_GREEN    "\x1b[32m"
#define COLOR_YELLOW   "\x1b[33m"
#define COLOR_BLUE     "\x1b[34m"
#define COLOR_MAGENTA  "\x1b[35m"
#define COLOR_CYAN     "\x1b[36m"
#define COLOR_FLUSH    "\x1b[0m"

#define MSG_OK(msg)       printf("[%sOK%s] %s\n",COLOR_GREEN, COLOR_FLUSH, msg);
#define MSG_ERR(msg)      fprintf(stderr, "[%sERROR%s] %s\n",COLOR_RED, COLOR_FLUSH, msg);
#define MSG_DEBUG(msg)    printf("[%sDEBUG%s] %s\n",COLOR_CYAN, COLOR_FLUSH, msg);
#define MSG_WARNING(msg)  fprintf(stderr, "[%sWARNING%s] %s\n",COLOR_YELLOW, COLOR_FLUSH, msg);
#define MSG_INFO(msg)     printf("[INFO] %s\n", msg);
#define MSG_IO(msg)       printf("[%sI/O%s] %s\n",COLOR_MAGENTA, COLOR_FLUSH, msg);


/*
void test()
{
	printf("Questa stringa e' per vedere un output a caso\n");
	MSG_INFO("Calling syscall fork()");
	MSG_OK("Child process created - PID: <pid>");
	MSG_INFO("Calling syscall fork()");
	MSG_ERR("fork() unknown error");
	MSG_DEBUG("Reached line 45 of file main.c");
	MSG_WARNING("Parent process waits until the child process terminate");
	MSG_INFO("Calling syscall fork()");
	MSG_IO("Child process wants to read from file conf.dat");
}
*/