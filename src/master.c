#define _GNU_SOURCE

#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include <limits.h>     /* Limits of numbers macros */ 
#include <string.h>     /* stderr */
#include <signal.h>		/* set_handler(), */
#include <errno.h>      /* errno */
#include <time.h>       /* time() */
#include "common.h"
#include "bashprint.h"

/* only used by get_configuration() as env. variable names */
char conf_names[N_RUNTIME_CONF_VALUES][22+1] = {
	"SO_USERS_NUM", "SO_NODES_NUM", "SO_BUDGET_INIT", "SO_REWARD",
	"SO_MIN_TRANS_GEN_NSEC", "SO_MAX_TRANS_GEN_NSEC", "SO_RETRY",
	"SO_TP_SIZE", "SO_MIN_TRANS_PROC_NSEC", "SO_MAX_TRANS_PROC_NSEC", 
	"SO_SIM_SEC", "SO_FRIENDS_NUM", "SO_HOPS"
};

/*
    TODO:
    -Id code di messaggi una per nodo dove andranno caricate le transazioni da immettere nel libro mastro scartando quelle in eccesso, creazione transaction pool
    -Creazione transazioni, metterle nella coda estratta a caso e continuare il ciclo
    -Ciclo dei nodi, che prenderanno le transazioni dalla coda quando ci saranno sufficenti transazioni per riempire un blocco del registro
    -Aggiungere segnale per scatenare una transazione da utente
    -Gestione della terminazione da Master: quando si verifica determinati eventi gestire killando nodi e processi e levare le ipc usate
*/

/* ----- PROTOTYPES ----- */

void init();
void init_conf();
void init_semaphores();
void init_sharedmem();
void get_configuration(unsigned long *conf);
void sigterm_handler(int signum);
int  check_termination();
void send_kill_signals();
void print_stats(int force_print);
void shutdown(int status);

/* ------- GLOBAL VARIABLES ------- */

int shmConfig; /* ID shmem configurazione */
int shmUsers;  /* ID shmem users data */
int shmNodes;  /* ID shmem nodes data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int *msgTransactions; /* ID msg queues transaction */
user *shmUsersArray;  /* ID shmem Array of User PIDs */
node *shmNodesArray;  /* ID shmem Array of Node PIDs */
transaction (*shmLibroMastroMatrix)[SO_REGISTRY_SIZE]; /* ID shmem Array of transactions */

/** Semaphore values
 * 0 write
 * 1 mutex
 * 2 readcount
 * **/
int semUsers;  /* Semaphore for shmem access on the Array of User PIDs */
int semNodes;  /* Semaphore for shmem access on the Array of Node PIDs */
int semLibroMastro;  /* Semaphore for shmem access on the Libro Mastro */
int semSimulazione;  /* Semaphore for the simulation */
unsigned long *conf; /* Array of conf. values attached to shmem */

int remaining_users; /* Number of active users */
int remaining_nodes; /* Number of active nodes */
time_t starting_time;

int main (int argc, char ** argv)
{
    pid_t child_pid, wpid; /* child_pid is used for the fork, wpid unused */

    /*** Certe robe vanno poi spostate in user.c e node.c, ora le metto per essere
     * standard C89 compliant ***/
    int status = 0; /*  */
    int fails = 0; /* used with SO_RETRY */
    int bilancio; /*  */
    transaction newTr; /* new transaction */
    int randomReceiver; /* Random user */
    int randomNode; /* Random node */
    int randomQuantity; /* Random quantity for the transaction */
    int nodeReward; /* Transaction reward */
    int termination_reason = 0;
    int i=0, j=0, k=0;

    struct timespec t;
    t.tv_sec = 1;
    t.tv_nsec = 0;

	/*system("clear"); */
    init();

	/* User creating loop */
    for (i = 0; i < conf[SO_USERS_NUM]; i++)
    {
        child_pid = fork();
        switch (child_pid)
        {
        case -1:
            perror("Fork user error ");
            exit(0);
            break;
        case 0:
            /* Child branch */

            /* Shmem write */
            initWriteInShm(semUsers);
            shmUsersArray = (user *)shmat(shmUsers, NULL, 0);
            if(shmat(shmUsers, NULL, 0) == (void *) -1)
                perror("shmUsersArray shmat ");
            if (shmUsersArray[i].pid == 0)
            {
                shmUsersArray[i].pid = getpid();
                shmUsersArray[i].budget = conf[SO_BUDGET_INIT];
            }
            printf("Child put %d in shmUsers[%d]\n", getpid(), i);
            shmdt((void *)shmUsersArray);
            if(shmUsersArray == (void *) -1)
                perror("shmUsersArray shmdt ");
            endWriteInShm(semUsers);

            /* Shmem read of user pids and budgets */
            initReadFromShm(semUsers);
            shmUsersArray = shmat(shmUsers, NULL, 0);
            if(shmUsersArray == (void *) -1)
                perror("shmUsersArray shmat ");
            for (k = 0; k < conf[SO_USERS_NUM]; k++)
            {
                printf("shmUsers[%d] value : %d, con budget %d\n", k, shmUsersArray[k].pid, 
							shmUsersArray[k].budget);
            }
            shmdt((void *)shmUsersArray);
             if(shmUsersArray == (void *) -1)
                perror("shmUsersArray shmdt ");
            endReadFromShm(semUsers);

            /* Waiting that the nodes and the users are ready and active */
            reserveSem(semSimulazione, 0);
            while (semctl(semSimulazione, 0, GETVAL, 0) != 0)
                sleep(1);

            /* execve() degli user */

            /* Starting user loop */
            while(fails < conf[SO_RETRY]) {
                /* TODO: controllo transazioni fallite */
                /* calcolo bilancio - lettura with and readers solution */
                initReadFromShm(semUsers);
                shmUsersArray = shmat(shmUsers, NULL, 0);
                if(shmUsersArray == (void *) -1)
                    perror("shmUsersArray shmat ");
                bilancio = shmUsersArray[i].budget;
                shmdt((void *) shmUsersArray);
                if(shmUsersArray == (void *) -1)
                    perror("shmUsersArray shmad ");
                endReadFromShm(semUsers);

                initReadFromShm(semLibroMastro);
                shmLibroMastroMatrix = shmat(shmLibroMastro, NULL, 0);
                if(shmLibroMastroMatrix == (void *) -1)
                    perror("shmLibroMastroMatrix shmat ");
                for (k = 0; k < conf[SO_REGISTRY_SIZE]; k++)
                {
                    for (j = 0; j < conf[SO_BLOCK_SIZE]; j++)
                    {
                        /* TODO: calcolo bilancio dev'essere calcolato anche in base alle transazioni in coda
                              contare anche i reward inviati ai nodi */
                        /* printf("LibroMastro[%d][%d] vale %d\n", k, j, shmLibroMastroMatrix[k][j].quantity); */

                        if(shmLibroMastroMatrix[k][j].receiver == getpid())
                            bilancio += shmLibroMastroMatrix[k][j].quantity;

                        if(shmLibroMastroMatrix[k][j].sender == getpid())
                            bilancio -= shmLibroMastroMatrix[k][j].quantity;
                    }
                    
                }
                shmdt((void *)shmLibroMastroMatrix);
                if(shmLibroMastroMatrix == (void *) -1)
                    perror("shmLibroMastroMatrix shmdt ");
                endReadFromShm(semLibroMastro);
                
                printf("[%d] ha bilancio uguale a : %d\n", getpid(), bilancio);
                if(bilancio >= 2) 
                {
                    shutdown(EXIT_SUCCESS);
                    /* TODO: genera transazione */
                    randomReceiver = randomNum(0, conf[SO_USERS_NUM]-1);
                    randomNode = randomNum(0, conf[SO_NODES_NUM]-1);
                    randomQuantity = randomNum(2, bilancio);
                    nodeReward = randomQuantity * conf[SO_REWARD] / 100;
                    randomQuantity -= nodeReward;
                    newTr.quantity = randomQuantity;
                    newTr.receiver = randomReceiver;
                    newTr.reward = nodeReward;
                    newTr.sender = getpid();
                    newTr.timestamp = (int)time(NULL);
                    
                } else {
                    fails++;
                }
            }
            exit(0);
            break;
        default:
            /* father branch */
            /* wait(NULL); */
            break;
        }
    }

    /* When this integer reaches 0, the simulation must end */
    remaining_users = conf[SO_USERS_NUM];

    /* Nodes creating loop */
    for (i = 0; i < conf[SO_NODES_NUM]; i++){
        child_pid = fork();
        switch (child_pid){
        case -1:
            perror("Fork node error ");
            exit(0);
            break;
        case 0:
            /* child branch */

            /* Shmem write */
            initWriteInShm(semNodes);
            shmNodesArray = (node *)shmat(shmNodes, NULL, 0);
            if(shmNodesArray == (void *) -1)
                perror("shmNodesArray shmat ");
            if (shmNodesArray[i].pid == 0)
            {
                shmNodesArray[i].pid = getpid();
                shmNodesArray[i].reward = 0;
            }
            printf("Child put %d in shmNodes[%d]\n", getpid(), i);
            shmdt((void *)shmNodesArray);
            if(shmNodesArray == (void *) -1)
                perror("shmNodesArray shmdt ");
            endWriteInShm(semNodes);

            /* Shmem read for Node pids and reward */
            initReadFromShm(semNodes);
            shmNodesArray = shmat(shmNodes, NULL, 0);
            for (k = 0; k < conf[SO_NODES_NUM]; k++)
            {
                printf("shmNodes[%d] value : %d, con reward %d\n", k, shmNodesArray[k].pid, 
							shmNodesArray[k].reward);
            }
            shmdt((void *)shmNodesArray);
            endReadFromShm(semNodes);

            /* Waiting that the other nodes are ready and active */
            reserveSem(semSimulazione, 0);
            while (semctl(semSimulazione, 0, GETVAL, 0) != 0)
               sleep(1);

            msgTransactions = (int*) malloc(conf[SO_NODES_NUM] * sizeof(int));
            msgTransactions[i] = msgget(ftok(FTOK_PATHNAME_NODE, getpid()), IPC_CREAT | IPC_EXCL | 0600);
            if(msgTransactions == -1){
                MSG_ERR("main: msgTransactions, error while creating the message queue.");
                perror("\tmsgTransactions");
                shutdown(EXIT_FAILURE);
            }

            /* execve() dei nodi */ 
            execve("./bin/node", NULL, NULL);

            exit(0);
            break;
        default:
            /* father branch */
            break;
        }
    }

    remaining_nodes = conf[SO_NODES_NUM];

	/* gestione CTRL+C e altri segnali */
	set_handler(SIGTERM, &sigterm_handler);
	set_handler(SIGINT, &sigterm_handler);
	set_handler(SIGALRM, &sigterm_handler);
	set_handler(SIGCHLD, &sigterm_handler);

    /* Attesa del semaforo per far partire la simulazione */

    // time(&starting_time); alarm
    while (1){
        nanosleep(&t, &t);
        if(termination_reason = check_termination()){
            send_kill_signals();

            /* Attesa che tutti i figli siano siano conclusi */
            while (wait(NULL) != -1);
            
            print_stats(1);
            shutdown(EXIT_SUCCESS);
        } else {
            print_stats(0);
        }
    }

	shutdown(EXIT_SUCCESS);
	return 0;
}

/*  */
void init()
{
    /*** flags 0600 ***/
    init_conf();
    init_semaphores();
	init_sharedmem();
    /* Message queues are created when generating nodes */
    /* Init seed for the random number generation */ 
    srand(time(NULL));
}

/*  */
void init_conf()
{
    /* Creating the shared memory segment */
    shmConfig = shmget(SHM_ENV_KEY, sizeof(unsigned long) * N_CONF_VALUES, 
                            IPC_CREAT | IPC_EXCL | 0666);
    if (shmConfig == -1){
        MSG_ERR("main: shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown(EXIT_FAILURE);
    }

    conf = shmat(shmConfig, NULL, 0);

    /* Gets conf from Env variables, Writes configuration to shared memory */
    get_configuration(conf);
}

/*  */
void init_semaphores()
{
    semUsers = semget(SEM_USER_KEY, 3, IPC_CREAT | 0666);
    if(semUsers == -1){
		MSG_ERR("main: semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
		shutdown(EXIT_FAILURE);
	}

	semNodes = semget(SEM_NODE_KEY, 3, IPC_CREAT | 0666);
    if(semNodes == -1){
		MSG_ERR("main: semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, IPC_CREAT | 0666);
    if(semLibroMastro == -1){
		MSG_ERR("main: semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}

    semSimulazione = semget(SEM_SIM_KEY, 1, IPC_CREAT | 0666);
    if(semSimulazione == -1){
		MSG_ERR("main: semSimulazione, error while creating the semaphore.");
        perror("\tsemSimulazione");
		shutdown(EXIT_FAILURE);
	}

    /* Inizializzazione semafori */
    /*if(initSemAvailable(semUsers, 0) == -1)
        perror("initSemAvailable(semUsers, 0) ");
    if(initSemAvailable(semUsers, 1) == -1)
        perror("initSemAvailable(semUsers, 1) ");
    if(initSemInUse(semUsers, 2) == -1)
        perror("initSemAvailable(semUsers, 2) ");

    if(initSemAvailable(semNodes, 0) == -1)
        perror("initSemAvaible(semNode, 0) ");
    if(initSemAvailable(semNodes, 1) == -1)
        perror("initSemAvaible(semNode, 1) ");
    if(initSemInUse(semNodes, 0) == -1)
        perror("initSemInUse(semNode, 2) ");

    if(initSemAvailable(semLibroMastro, 0) == -1)
        perror("initSemAvaible(semLibroMastro, 0) ");
    if(initSemAvailable(semLibroMastro, 1) == -1)
        perror("initSemAvaible(semLibroMastro, 1) ");
    if(initSemInUse(semLibroMastro, 0) == -1)
        perror("initSemInUse(semLibroMastro, 2) ");

    if( initSemSimulation(semSimulazione, 0) == -1)
        perror("initSemSimulation(semSimulazione, 0) ");*/

	initSemAvailable(semUsers, 0);
    initSemAvailable(semUsers, 1);
    initSemInUse(semUsers, 2);

    initSemAvailable(semNodes, 0);
    initSemAvailable(semNodes, 1);
    initSemInUse(semNodes, 2);
    
    initSemAvailable(semLibroMastro, 0);
    initSemAvailable(semLibroMastro, 1);
    initSemInUse(semLibroMastro, 2);

    initSemSimulation(semSimulazione, 0, conf[SO_USERS_NUM], conf[SO_NODES_NUM]);
}

/*  */
void init_sharedmem()
{
    /* Creating shmem segment for Users */
    shmUsers = shmget(SHM_USER_KEY, sizeof(user) * conf[SO_USERS_NUM], IPC_CREAT | IPC_EXCL | 0666);
    if (shmUsers == -1){
		MSG_ERR("main: shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
		shutdown(EXIT_FAILURE);
	}

    /* Creating shmem segment for Nodes */
    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], IPC_CREAT | IPC_EXCL | 0666);
    if (shmNodes == -1){
		MSG_ERR("main: shmNodes, error while creating the shared memory segment.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

    /* Creating shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(transaction[conf[SO_REGISTRY_SIZE]][conf[SO_BLOCK_SIZE]]),
							 IPC_CREAT | IPC_EXCL | 0666);
    if (shmLibroMastro == -1){
		MSG_ERR("main: shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}
}

/*  */
void get_configuration(unsigned long * conf)
{
	char *env_var_val = NULL;    /* used to read env variables */
	char *chk_strtol_err = NULL; /* used to detect errors when converting str to ulong */
	char i = 0;
#ifdef DEBUG
	MSG_INFO2("Checking compile time parameters...");
#endif
	/* Check compile time parameters */
#ifndef SO_BLOCK_SIZE
	MSG_ERR("SO_BLOCK_SIZE is not defined!");
	exit(EXIT_FAILURE);
#endif
#ifndef SO_REGISTRY_SIZE
	MSG_ERR("SO_REGISTRY_SIZE is not defined!");
	exit(EXIT_FAILURE);
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
	for (i = SO_USERS_NUM; i < N_RUNTIME_CONF_VALUES; i++) {
		if(env_var_val = getenv(conf_names[i])){
			errno = 0;
			conf[i] = strtoul(env_var_val, &chk_strtol_err, 10);
			/* check conversion */
			if((conf[i] == 0 && (errno == EINVAL || errno == ERANGE)) || env_var_val == chk_strtol_err) {
				fprintf(stderr, "[%sERROR%s] Could not convert env variable %s to an unsigned long\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
				exit(EXIT_FAILURE);
			}
			/* check valid number */
			if(i == SO_MAX_TRANS_GEN_NSEC && conf[SO_MAX_TRANS_GEN_NSEC] < conf[SO_MIN_TRANS_GEN_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_GEN_NSEC is lower than SO_MIN_TRANS_GEN_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_MAX_TRANS_PROC_NSEC && conf[SO_MAX_TRANS_PROC_NSEC] < conf[SO_MIN_TRANS_PROC_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_PROC_NSEC is lower than SO_MIN_TRANS_PROC_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_TP_SIZE && conf[SO_TP_SIZE] <= SO_BLOCK_SIZE) {
				MSG_ERR("SO_TP_SIZE is not bigger than SO_BLOCK_SIZE!");
				exit(EXIT_FAILURE);
			} else if (i == SO_REWARD && (conf[SO_REWARD] < 0 || conf[SO_REWARD] > 100)) {
				MSG_ERR("SO_REWARD is out range [0-100]!");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "[%sERROR%s] Undefined environment variable %s. Make sure to load env. variables first!\n"
							"        Example: source cfg/custom.cfg\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
			exit(EXIT_FAILURE);
		}
	}
    conf[SO_BLOCK_SIZE] = SO_BLOCK_SIZE;
    conf[SO_REGISTRY_SIZE] = SO_REGISTRY_SIZE;
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
}

/* non funge */
void sigterm_handler(int signum) 
{
	fprintf(stdout, "[%sINFO%s] Ricevuto il segnale %s, arresto la simulazione\n",
				COLOR_YELLOW, COLOR_FLUSH, strsignal(signum));
	shutdown(EXIT_SUCCESS);
}

/* procedura di terminazione, clear IPC, ... */
void shutdown(int status) 
{
    int i = 0;
    /* detach the shmem of the conf */
    shmdt((void *)conf);
    if(conf == (void *) -1){
        MSG_ERR("main: conf, error while detaching the configuration shmem segment.");
		shutdown(EXIT_FAILURE);
	}
	/* Rimozione IPC */
	shmctl(shmUsers, IPC_RMID, NULL);
	shmctl(shmNodes, IPC_RMID, NULL);
	shmctl(shmLibroMastro, IPC_RMID, NULL);
	shmctl(shmConfig, IPC_RMID, NULL);

	semctl(semUsers, 0, IPC_RMID, 0);
	semctl(semNodes, 0, IPC_RMID, 0);
	semctl(semLibroMastro, 0, IPC_RMID, 0);
	semctl(semSimulazione, 0, IPC_RMID, 0);

    /* Removing Msg Queues */
    for(i=0; i < conf[SO_NODES_NUM]; i++)
	    msgctl(msgTransactions[i], IPC_RMID, NULL);
    free(msgTransactions);

    exit(status);
}

/* this function checks termination */
int check_termination()
{
    /* Reason (1): time > SO_SIM_SEC */
    if(time(NULL) - starting_time >= conf[SO_SIM_SEC]){
        printf("Ending simulation: The execution lasted SO_SIM_SEC=%ld seconds.\n", conf[SO_SIM_SEC]);
        return 1;
    }
    
    /* Reason (2): Libro Mastro is full, block_number == SO_REGISTRY_SIZE ?? */

    /* Reason (3): No more active users */
    if(remaining_users == 0){
        printf("Ending simulation: No more active users.\n");
        return 3;
    }
}

/*  */
void send_kill_signals()
{
    /* In questo caso kill() esegue unicamente un controllo degli
errori per vedere se è possibile inviare segnali al processo: il
null signal può essere utilizzato per testare se un processo con
un certo pid esiste, controllo ESRCH, EPERM */
    /* For each PID in Users array, send signal */

    /* For each PID in Nodes array, send signal */

}

/*  */
void print_stats(int force_print)
{

}

