#ifndef _STDLIB_H
#include <stdlib.h>
#endif
#ifndef _SIGNAL_H
#include <signal.h>
#endif
#ifndef _TIME_H
#include <time.h>
#endif
#ifndef _SYS_TYPES_H
#include <sys/types.h>
#endif
#ifndef _SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifndef _SYS_IPC_H
#include <sys/ipc.h>
#endif
#ifndef _SYS_SEM_H
#include <sys/sem.h>
#endif
#ifndef _SYS_SHM_H
#include <sys/shm.h>
#endif
#ifndef _SYS_MSG_H
#include <sys/msg.h>
#endif

#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include <limits.h>     /* Limits of numbers macros */ 
#include <string.h>     /* stderr */
#include <signal.h>		/* set_handler(), */
#include <errno.h>      /* errno */
#include "bashprint.h"  /* Pretty print messages to screen */

#ifndef __COMMON_H
#define __COMMON_H 1

/*** IPC Keys ***/

#define SHM_USER_KEY 1004
#define SHM_NODE_KEY 1826
#define SHM_LIBROMASTRO_KEY 9001
#define SHM_ENV_KEY 98120

#define SEM_USER_KEY 76543
#define SEM_NODE_KEY 2009
#define SEM_LIBROMASTRO_KEY 65432
#define SEM_SIM_KEY 82141

#define MSG_TRANS_KEY 62132

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
#if defined(__linux__)
    struct seminfo *__buf;
#endif
};

/*** Custom Data Structures ***/

/* Users type */
typedef struct
{
    int pid;
    int budget;
} user;

/* Ndoes type */
typedef struct
{
    int pid;
    int reward;
} node;

/* Transaction type */
typedef struct
{
    double timestamp;
    int sender;
    int receiver;
    int quantity;
    int reward;
} transaction;

/*** Semaphore Management ***/

int initSemAvailable(int, int);
int initSemInUse(int, int);
int reserveSem(int, int);
int releaseSem(int, int);
int waitSem(int, int);
int initSemSimulation(int, int, int, int);

/*** Signals Management ***/

sigset_t block_signals(int count, ...);
sigset_t unblock_signals(int count, ...);
void reset_signals(sigset_t old_mask);
struct sigaction set_handler(int sig, void (*func)(int));

/*** Shared Memory Management ***/

void initReadFromShm(int);
void endReadFromShm(int);
void initWriteInShm(int);
void endWriteInShm(int);

/*** Random Number Utility ***/

int randomNum(int, int);


#define N_RUNTIME_CONF_VALUES 13
/* RUN TIME CONFIGURATION VALUES */
enum conf_index {
	SO_USERS_NUM, SO_NODES_NUM, SO_BUDGET_INIT, SO_REWARD, 
	SO_MIN_TRANS_GEN_NSEC, SO_MAX_TRANS_GEN_NSEC, SO_RETRY, 
	SO_TP_SIZE, SO_MIN_TRANS_PROC_NSEC, SO_MAX_TRANS_PROC_NSEC, 
	SO_SIM_SEC, SO_FRIENDS_NUM, SO_HOPS
};

void get_configuration();

#endif /* __COMMON_H */


