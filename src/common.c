#define _GNU_SOURCE

#include "common.h"
#ifndef _STDARG_H
#include <stdarg.h>
#endif

#pragma region SEMAPHORE_MANAGEMENT

/* Initialize semaphore to 1 (i.e., "available")*/
int initSemAvailable(int semId, int semNum)
{
	union semun arg;
	arg.val = 1;
	return semctl(semId, semNum, SETVAL, arg);
}

/* Initialize semaphore to 0 (i.e., "in use") */
int initSemInUse(int semId, int semNum)
{
	union semun arg;
	arg.val = 0;
	return semctl(semId, semNum, SETVAL, arg);
}

/* Reserve semaphore - decrement it by 1 */
int reserveSem(int semId, int semNum)
{
	struct sembuf sops;
	sops.sem_num = semNum;
	sops.sem_op = -1;
	sops.sem_flg = 0;

	return semop(semId, &sops, 1);
}

/* Release semaphore - increment it by 1 */
int releaseSem(int semId, int semNum)
{
	struct sembuf sops;
	sops.sem_num = semNum;
	sops.sem_op = 1;
	sops.sem_flg = 0;
	return semop(semId, &sops, 1);
}

/* Semaphore in wait state */
int waitSem(int semId, int semNum)
{
	struct sembuf sops;
	sops.sem_num = semNum;
	sops.sem_op = 0;
	sops.sem_flg = 0;
	return semop(semId, &sops, 1);
}

/* Init simulation semaphore */
int initSemSimulation(int semId, int semNum, int usersNum, int nodesNum)
{
	int total = (usersNum + nodesNum);
	struct sembuf sops;
	sops.sem_num = semNum;
	sops.sem_op = total;
	sops.sem_flg = 0;
	return semop(semId, &sops, 1);
}

#pragma endregion /* SEMAPHORE_MANAGEMENT */

#pragma region SIGNALS_MANAGEMENT

/* Blocca i segnali elencati tra gli argomenti */
/* Restituisce la vecchia maschera */
sigset_t block_signals(int count, ...)
{
	sigset_t mask, old_mask;
	va_list argptr;
	int i;

	sigemptyset(&mask);

	va_start(argptr, count);

	for (i = 0; i < count; i++)
	{
		sigaddset(&mask, va_arg(argptr, int));
	}

	va_end(argptr);

	sigprocmask(SIG_BLOCK, &mask, &old_mask);
	return old_mask;
}

/* Sblocca i segnali elencati tra gli argomenti */
/* Restituisce la vecchia maschera */
sigset_t unblock_signals(int count, ...)
{
	sigset_t mask, old_mask;
	va_list argptr;
	int i;

	sigemptyset(&mask);

	va_start(argptr, count);

	for (i = 0; i < count; i++)
	{
		sigaddset(&mask, va_arg(argptr, int));
	}

	va_end(argptr);

	sigprocmask(SIG_UNBLOCK, &mask, &old_mask);
	return old_mask;
}

/*
 * Imposta una maschera per i segnali, usata per reimpostare una
 * vecchia maschera ritornata da block_signals
 */
void reset_signals(sigset_t old_mask)
{
	sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

/* Imposta un nuovo handler per il segnale sig */
/* Ritorna il vecchio struct sigaction */
struct sigaction set_handler(int sig, void (*func)(int))
{
	struct sigaction sa, sa_old;
	sigset_t mask;
	sigemptyset(&mask);
	sa.sa_handler = func;
	sa.sa_mask = mask;
	sa.sa_flags = SA_NODEFER;
	sigaction(sig, &sa, &sa_old);
	return sa_old;
}

#pragma endregion /* SIGNALS_MANAGEMENT */

#pragma region SHARED_MEM_MANAGEMENT

/*** Inizio Writers Readers Problem Solution
 *  write 0
 *  mutex 1
 *  readerscount 2
 *  ***/

/** Lettura
 *  Write e mutex inizializzati ad 1 e rc a 0. Mutex ci assicura l'esclusione mutuale
 * 	e write si occupa delle scritture ed è utilizzato sia da reader che da writer.
 * 	Readerscount descrive il numero di processi readers che hanno accesso alla risorsa.
 *  Quando rc è uguale a 1 i writer aspettano. Con questo ci assicuriamo che i writer
 * 	non possono accedere alla risorsa finché un reader sta leggendo la stessa.
 *  Quando il reader ha finito rc viene decrementato, quando è uguale a 0 un writer
 *  può accederci.
 * **/
void initReadFromShm(int semId)
{
	while (semctl(semId, 1, GETVAL, 0) == 0)
		;
	releaseSem(semId, 2);
	if (semctl(semId, 2, GETVAL, 0) == 1)
	{
		while (semctl(semId, 0, GETVAL, 0) == 0)
			;
	}
	releaseSem(semId, 1);
}

void endReadFromShm(int semId)
{
	while (semctl(semId, 1, GETVAL, 0) == 0)
		;
	reserveSem(semId, 2);
	if (semctl(semId, 2, GETVAL, 0) == 0)
	{
		releaseSem(semId, 0);
	}
	releaseSem(semId, 1);
}

/** Scrittura
 * Se un writer vuole accedere l'oggetto aspetterà il semaforo write uguale a 0.
 * Come inizia la sua scrittura si riserva il semaforo e nessun altro writer
 * può accederci.
 * **/
void initWriteInShm(int semId)
{
    while (semctl(semId, 0, GETVAL, 0) == 0)
        ;
    reserveSem(semId, 0);
}

void endWriteInShm(int semId)
{
	releaseSem(semId, 0);
}

#pragma endregion /* SHARED_MEM_MANAGEMENT */

/* Useful random number function */
int randomNum(int min, int max)
{
	return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

int get_configuration(unsigned long *conf)
{
	char conf_names[N_RUNTIME_CONF_VALUES][22 + 1] = {
		"SO_USERS_NUM", "SO_NODES_NUM", "SO_BUDGET_INIT", "SO_REWARD",
		"SO_MIN_TRANS_GEN_NSEC", "SO_MAX_TRANS_GEN_NSEC", "SO_RETRY",
		"SO_TP_SIZE", "SO_MIN_TRANS_PROC_NSEC", "SO_MAX_TRANS_PROC_NSEC",
		"SO_SIM_SEC", "SO_FRIENDS_NUM", "SO_HOPS"};
	char *env_var_val = NULL;	 /* used to read env variables */
	char *chk_strtol_err = NULL; /* used to detect errors when converting str to ulong */
	char i = 0;
#ifdef DEBUG
	MSG_INFO2("Checking compile time parameters...");
#endif
	/* Check compile time parameters */
#ifndef SO_BLOCK_SIZE
	MSG_ERR("SO_BLOCK_SIZE is not defined!");
	return 0;
#endif
#ifndef SO_REGISTRY_SIZE
	MSG_ERR("SO_REGISTRY_SIZE is not defined!");
	return 0;
#endif
#ifdef DEBUG
	printf("---------------------------------------------------\n");
	printf("|                   COMPILE TIME                  |\n");
	printf("---------------------------------------------------\n");
	printf("|    SO_BLOCK_SIZE             |    %10u    |\n", SO_BLOCK_SIZE);
	printf("|    SO_REGISTRY_SIZE          |    %10u    |\n", SO_REGISTRY_SIZE);
	printf("---------------------------------------------------\n");
	MSG_OK("Compile time parameters exist!");
	MSG_INFO2("Checking run time parameters...");
	printf("---------------------------------------------------\n");
	printf("|                   RUNNING TIME                  |\n");
	printf("---------------------------------------------------\n");
#endif
	for (i = SO_USERS_NUM; i < N_RUNTIME_CONF_VALUES; i++)
	{
		if (env_var_val = getenv(conf_names[i]))
		{
			errno = 0;
			conf[i] = strtoul(env_var_val, &chk_strtol_err, 10);
			/* check conversion */
			if ((conf[i] == 0 && (errno == EINVAL || errno == ERANGE)) || env_var_val == chk_strtol_err)
			{
				fprintf(stderr, "[%sERROR%s] Could not convert env variable %s to an unsigned long\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
				return 0;
			}
			/* check valid number */
			if (i == SO_MAX_TRANS_GEN_NSEC && conf[SO_MAX_TRANS_GEN_NSEC] < conf[SO_MIN_TRANS_GEN_NSEC])
			{
				MSG_ERR("SO_MAX_TRANS_GEN_NSEC is lower than SO_MIN_TRANS_GEN_NSEC!");
				return 0;
			}
			else if (i == SO_MAX_TRANS_PROC_NSEC && conf[SO_MAX_TRANS_PROC_NSEC] < conf[SO_MIN_TRANS_PROC_NSEC])
			{
				MSG_ERR("SO_MAX_TRANS_PROC_NSEC is lower than SO_MIN_TRANS_PROC_NSEC!");
				return 0;
			}
			else if (i == SO_TP_SIZE && conf[SO_TP_SIZE] <= SO_BLOCK_SIZE)
			{
				MSG_ERR("SO_TP_SIZE is not bigger than SO_BLOCK_SIZE!");
				return 0;
			}
			else if (i == SO_REWARD && (conf[SO_REWARD] < 0 || conf[SO_REWARD] > 100))
			{
				MSG_ERR("SO_REWARD is out range [0-100]!");
				return 0;
			}
		}
		else
		{
			fprintf(stderr, "[%sERROR%s] Undefined environment variable %s. Make sure to load env. variables first!\n"
							"        Example: source cfg/custom.cfg\n",
					COLOR_RED, COLOR_FLUSH, conf_names[i]);
			return 0;
		}
	}
#ifdef DEBUG
	i = SO_USERS_NUM;
	printf("|    SO_USERS_NUM              |    %10u    |\n", conf[i++]);
	printf("|    SO_NODES_NUM              |    %10u    |\n", conf[i++]);
	printf("|    SO_BUDGET_INIT            |    %10u    |\n", conf[i++]);
	printf("|    SO_REWARD                 |    %10u    |\n", conf[i++]);
	printf("|    SO_MIN_TRANS_GEN_NSEC     |    %10u    |\n", conf[i++]);
	printf("|    SO_MAX_TRANS_GEN_NSEC     |    %10u    |\n", conf[i++]);
	printf("|    SO_RETRY                  |    %10u    |\n", conf[i++]);
	printf("|    SO_TP_SIZE                |    %10u    |\n", conf[i++]);
	printf("|    SO_MIN_TRANS_PROC_NSEC    |    %10u    |\n", conf[i++]);
	printf("|    SO_MAX_TRANS_PROC_NSEC    |    %10u    |\n", conf[i++]);
	printf("|    SO_SIM_SEC                |    %10u    |\n", conf[i++]);
	printf("|    SO_FRIENDS_NUM            |    %10u    |\n", conf[i++]);
	printf("|    SO_HOPS                   |    %10u    |\n", conf[i]);
	printf("---------------------------------------------------\n");
	MSG_OK("Running time parameters retrieved successfully!");
	printf("Press any button to continue...");
	getchar();
#endif
	return 1;
}