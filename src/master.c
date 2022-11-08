#define _GNU_SOURCE

#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include <limits.h>     /* Limits of numbers macros */ 
#include <string.h>     /* stderr */
#include <signal.h>		/* kill(), SIG* */
#include <errno.h>      /* errno */
#include <time.h>       /* time(), struct timespec */
#include "common.h"
#include "bashprint.h"

/* Force to print all the stats at the end of the simulation */
#define FORCE_PRINT_STATS 1
/* The print_stats(int) function will just print the useful info */
#define PRINT_USEFUL_STATS 0

/* only used by get_configuration() as env. variable names */
char conf_names[N_RUNTIME_CONF_VALUES][22+1] = {
	"SO_USERS_NUM", "SO_NODES_NUM", "SO_BUDGET_INIT", "SO_REWARD",
	"SO_MIN_TRANS_GEN_NSEC", "SO_MAX_TRANS_GEN_NSEC", "SO_RETRY",
	"SO_TP_SIZE", "SO_MIN_TRANS_PROC_NSEC", "SO_MAX_TRANS_PROC_NSEC", 
	"SO_SIM_SEC", "SO_FRIENDS_NUM", "SO_HOPS"
};

/* -------------------- PROTOTYPES -------------------- */

/* Initialization */
void init();
void init_conf();
void init_semaphores();
void init_sharedmem();
void get_configuration(unsigned long *conf);
void users_generation();
void nodes_generation();

/* Lifetime */
void print_stats(int force_print);

/* Signal Handlers  */
void sigterm_handler(int signum);
void sigusr1_handler(int signum);
void sigalrm_handler(int signum);
void sigchld_handler(int signum);

/* Termination */
void send_kill_signals();
void shutdown(int status);
void clean_end();

/* -------------------- GLOBAL VARIABLES -------------------- */

/**** SHARED MEMORY IDs ****/
int shmConfig;        /* ID shmem configuration */
int shmUsers;         /* ID shmem users data */
int shmNodes;         /* ID shmem nodes data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int shmBlockNumber;   /* ID shmem libro mastro block number */

/**** SHARED MEMORY ATTACHED VARIABLES ****/
user *shmUsersArray;          /* Shmem Array of User PIDs */
node *shmNodesArray;          /* Shmem Array of Node PIDs */
block *libroMastroArray;      /* Shmem Array of blocks */
unsigned int *block_number;   /* Shmem number of the last block */
unsigned long *conf;          /* Shmem Array of configuration values */

/**** MESSAGE QUEUE IDs ****/
int *msgTransactions; /* Msg queues IDs Array */

/**** SEMAPHORE IDs ****/
int semUsers;        /* Semaphore for shmem access on the Array of User PIDs */
int semNodes;        /* Semaphore for shmem access on the Array of Node PIDs */
int semLibroMastro;  /* Semaphore for shmem access on the Libro Mastro */
int semSimulation;   /* Semaphore for the simulation */
int semBlockNumber;  /* Semaphore for the last block number */

/**** STATISTICAL VARIABLES ****/
int remaining_users; /* Number of active users */
int remaining_nodes; /* Number of active nodes */


int main (int argc, char ** argv)
{
    /* To print the stats every second */
    struct timespec t;
    t.tv_sec = 1;
    t.tv_nsec = 0;

	/*system("clear"); */

    /* (1): Get simulation configuration, initialize IPC Objects */
    init();

    /* (2): Generate child processes */
    nodes_generation();
    /* For statistical purposes */
    remaining_nodes = conf[SO_NODES_NUM];
    
    users_generation();
    /* When this integer reaches 0, the simulation must end */
    remaining_users = conf[SO_USERS_NUM];

    /* 
     * (3): Signal handling, signals to the Master are used to determine
     *      termination conditions.
     *      - Print stats every second. 
     */
	set_handler(SIGTERM, sigterm_handler);
	set_handler(SIGINT,  sigterm_handler);
	set_handler(SIGALRM, sigalrm_handler);
	set_handler(SIGCHLD, sigchld_handler);
	set_handler(SIGUSR1, sigusr1_handler);

    /* ????????????? */
    /* Attesa del semaforo per far partire la simulazione */
    /* ????????????? */
    
    alarm(conf[SO_SIM_SEC]);
    while (1){
        nanosleep(&t, &t);
        print_stats(PRINT_USEFUL_STATS);
    }

    /*
     * (4): Clear IPC object, Memory Free, Termination are managed by
     *      the termination functions.
     */ 

	return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

#pragma region INITIALIZATION
/* TODO: flags 0600  */

/* Wrapper function that calls the other initialization functions  */
void init()
{
    init_conf();
    init_semaphores();
	init_sharedmem();

    /* 
     * Message queues are created in nodes_generation(),
     * this is just the array of Msg queue IDs
     */
    msgTransactions = (int*) malloc(conf[SO_NODES_NUM] * sizeof(int));
    /* Initializes seed for the random number generation */ 
    srand(time(NULL));
}

/* Gets the configuration and write it in shared memory */
void init_conf()
{
    /* Creating the shared memory segment */
    shmConfig = shmget( SHM_ENV_KEY, 
                        sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
                        IPC_CREAT | IPC_EXCL | 0666);
    if (shmConfig == -1){
        MSG_ERR("master.init(): shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown(EXIT_FAILURE);
    }

    conf = shmat(shmConfig, NULL, 0);

    /* Gets conf from Env variables, Writes configuration to shared memory */
    get_configuration(conf);
}

/* Creates the semaphores and initializes them */
void init_semaphores()
{
    semUsers = semget(SEM_USER_KEY, 3, IPC_CREAT | 0666);
    if(semUsers == -1){
		MSG_ERR("master.init(): semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
		shutdown(EXIT_FAILURE);
	}

	semNodes = semget(SEM_NODE_KEY, 3, IPC_CREAT | 0666);
    if(semNodes == -1){
		MSG_ERR("master.init(): semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, IPC_CREAT | 0666);
    if(semLibroMastro == -1){
		MSG_ERR("master.init(): semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}

    semBlockNumber = semget(SEM_BLOCK_NUMBER, 1, IPC_CREAT | 0666);
    if(semBlockNumber == -1){
		MSG_ERR("master.init(): semBlockNumber, error while creating the semaphore.");
        perror("\tsemBlockNumber");
		shutdown(EXIT_FAILURE);
	}

    semSimulation = semget(SEM_SIM_KEY, 1, IPC_CREAT | 0666);
    if(semSimulation == -1){
		MSG_ERR("master.init(): semSimulation, error while creating the semaphore.");
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

    if( initSemSimulation(semSimulation, 0) == -1)
        perror("initSemSimulation(semSimulation, 0) ");*/

	initSemAvailable(semUsers, 0);
    initSemAvailable(semUsers, 1);
    initSemInUse(semUsers, 2);

    initSemAvailable(semNodes, 0);
    initSemAvailable(semNodes, 1);
    initSemInUse(semNodes, 2);
    
    initSemAvailable(semLibroMastro, 0);
    initSemAvailable(semLibroMastro, 1);
    initSemInUse(semLibroMastro, 2);

    initSemSimulation(semSimulation, 0, conf[SO_USERS_NUM], 
                      conf[SO_NODES_NUM]);

    initSemAvailable(semBlockNumber, 0);
}

/* Creates the shmem segments */
void init_sharedmem()
{
    /* Creating shmem segment for Users */
    shmUsers = shmget(  SHM_USER_KEY, 
                        sizeof(user) * conf[SO_USERS_NUM], 
                        IPC_CREAT | IPC_EXCL | 0666);
    if (shmUsers == -1){
		MSG_ERR("master.init(): shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
		shutdown(EXIT_FAILURE);
	}
    shmUsersArray = (user *)shmat(shmUsers, NULL, 0);

    /* Creating shmem segment for Nodes */
    shmNodes = shmget(  SHM_NODE_KEY, 
                        sizeof(node) * conf[SO_NODES_NUM], 
                        IPC_CREAT | IPC_EXCL | 0666);
    if (shmNodes == -1){
		MSG_ERR("master.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes");
		shutdown(EXIT_FAILURE);
	}
    shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

    /* Creating shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, 
                            sizeof(block) * SO_REGISTRY_SIZE,
                            IPC_CREAT | IPC_EXCL | 0666);
    if (shmLibroMastro == -1){
		MSG_ERR("master.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tshmLibroMastro");
		shutdown(EXIT_FAILURE);
	}
    libroMastroArray = (block *)shmat(shmLibroMastro, NULL, 0);

    /* Creating shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, 
                            sizeof(unsigned int), 
                            IPC_CREAT | IPC_EXCL | 0666);
    if (shmBlockNumber == -1){
		MSG_ERR("master.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber");
		shutdown(EXIT_FAILURE); 
	}
    /* Block number initialization */
    block_number = (unsigned int *)shmat(shmBlockNumber, NULL, 0);
    *block_number = 0;
}

/* Reads the environment variables for the configuration  */
void get_configuration(unsigned long * conf)
{
    /* used to read env variables */
	char *env_var_val = NULL;
    /* used to detect errors when converting str to ulong */
	char *chk_strtol_err = NULL;
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
			if((conf[i] == 0 && (errno == EINVAL || errno == ERANGE)) 
                || env_var_val == chk_strtol_err) {
				fprintf(stderr, "[%sERROR%s] Could not convert env variable %s to an unsigned long\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
				exit(EXIT_FAILURE);
			}
			/* check valid number */
			if(i == SO_MAX_TRANS_GEN_NSEC && conf[SO_MAX_TRANS_GEN_NSEC] 
                < conf[SO_MIN_TRANS_GEN_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_GEN_NSEC is lower than SO_MIN_TRANS_GEN_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_MAX_TRANS_PROC_NSEC 
                        && conf[SO_MAX_TRANS_PROC_NSEC] 
                        < conf[SO_MIN_TRANS_PROC_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_PROC_NSEC is lower than SO_MIN_TRANS_PROC_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_TP_SIZE && conf[SO_TP_SIZE] <= SO_BLOCK_SIZE) {
				MSG_ERR("SO_TP_SIZE is not bigger than SO_BLOCK_SIZE!");
				exit(EXIT_FAILURE);
			} else if(i == SO_REWARD && (conf[SO_REWARD] < 0 
                        || conf[SO_REWARD] > 100)) {
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

/* Generates user child processes and initializes their shmem data structures */
void users_generation()
{
    pid_t child_pid; /* child_pid is used for the fork */
    int i=0, j=0, k=0;

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
            if (shmUsersArray[i].pid == 0)
            {
                shmUsersArray[i].pid = getpid();
                shmUsersArray[i].budget = conf[SO_BUDGET_INIT];
            }
            printf("Child put %d in shmUsers[%d]\n", getpid(), i);
            endWriteInShm(semUsers);

            /* Shmem read of user pids and budgets */
            initReadFromShm(semUsers);
            for (k = 0; k < conf[SO_USERS_NUM]; k++)
            {
                printf("shmUsers[%d] value : %d, con budget %d\n", k, 
                            shmUsersArray[k].pid, 
                            shmUsersArray[k].budget);
            }
            endReadFromShm(semUsers);

            /* Waiting that the nodes and the users are ready and active */
            reserveSem(semSimulation, 0);
            while (semctl(semSimulation, 0, GETVAL, 0) != 0)
                sleep(1);

            execve("./bin/user", NULL, NULL);

            /* exit(EXIT_SUCCESS); */
            break;
        default:
            /* father branch */
            /* wait(NULL); */
            break;
        }
    }
}

/* Generates node child processes and initializes their shmem data structures */
void nodes_generation()
{
    pid_t child_pid; /* child_pid is used for the fork */
    int i=0, j=0, k=0;

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
            if (shmNodesArray[i].pid == 0)
            {
                shmNodesArray[i].pid = getpid();
                shmNodesArray[i].reward = 0;
            }
            printf("Child put %d in shmNodes[%d]\n", getpid(), i);
            endWriteInShm(semNodes);

            /* Shmem read for Node pids and reward */
            initReadFromShm(semNodes);
            for (k = 0; k < conf[SO_NODES_NUM]; k++)
            {
                printf("shmNodes[%d] value : %d, con reward %d\n", k, 
                            shmNodesArray[k].pid, 
							shmNodesArray[k].reward);
            }
            endReadFromShm(semNodes);

            /* Waiting that the other nodes are ready and active */
            reserveSem(semSimulation, 0);
            while (semctl(semSimulation, 0, GETVAL, 0) != 0)
               sleep(1);

            msgTransactions[i] = msgget(ftok(FTOK_PATHNAME_NODE, getpid()), 
                                            IPC_CREAT | IPC_EXCL | 0666);
            if(msgTransactions == (void *) -1){
                MSG_ERR("main: msgTransactions, error while creating the message queue.");
                perror("\tmsgTransactions");
                shutdown(EXIT_FAILURE);
            }

            /* execve() dei nodi */ 
            execve("./bin/node", NULL, NULL);

            /* exit(EXIT_SUCCESS); */
            break;
        default:
            /* father branch */
            break;
        }
    }
}

#pragma endregion /* INITIALIZATION */

/* -------------------- LIFETIME FUNCTIONS -------------------- */

#pragma region LIFETIME_AND_SIGNAL_HANDLERS

/* TODO: read PDF for print_stats() details */
/* Prints the useful stats in lifetime, Prints all the info before exit() */
void print_stats(int force_print)
{
    int i = 0, j = 0;
    if(force_print){
        initReadFromShm(semUsers);
        printf("\n\n===============USERS==============\n");
        for(i = 0; i < conf[SO_USERS_NUM]; i++){
            printf("\tPID:%d\n", shmUsersArray[i].pid);
            printf("\tBudget: %d\n\n", shmUsersArray[i].budget);
        }
        endReadFromShm(semUsers);

        initReadFromShm(semLibroMastro);
        initReadFromShm(semBlockNumber);
        printf("\n\n===============BLOCKCHAIN==============\n");
        printf("# of blocks: %d\n", *block_number);

        for(i = 0; i < *block_number; i++){
            printf("Block #%d:\n", i);
            for(j = 0; j < SO_BLOCK_SIZE; j++){
                printf("\tTransaction #%d: t=%d\t snd=%d\t rcv=%d\t qty=%d\t rwd=%d\n",
                        j, libroMastroArray[i].transBlock[j].timestamp,
                        libroMastroArray[i].transBlock[j].sender,
                        libroMastroArray[i].transBlock[j].receiver,
                        libroMastroArray[i].transBlock[j].quantity,
                        libroMastroArray[i].transBlock[j].reward);
            }
        }
        endReadFromShm(semBlockNumber);
        endReadFromShm(semLibroMastro);
    }
    else{
        initReadFromShm(semUsers);
        printf("\n\n===============USERS==============\n");
        for(i = 0; i < conf[SO_USERS_NUM]; i++){
            printf("\tPID:%d\n", shmUsersArray[i].pid);
            printf("\tBudget: %d\n\n", shmUsersArray[i].budget);
        }
        endReadFromShm(semUsers);
    }
}

/* -------------------- SIGNAL HANDLERS -------------------- */
/* TODO: think of the SIGTERM, SIGINT handlers */
/* Debug signal */
void sigterm_handler(int signum) 
{
	fprintf(stdout, 
            "[INFO] Ricevuto il segnale %s, arresto la simulazione\n",
            strsignal(signum));
    clean_end();
}

/* Received when the blockchain is full */
void sigusr1_handler(int signum)
{
    /* Reason (1) for termination: The blockchain is full */
    printf("Ending simulation: The blockchain is full -> [%d/%ld]\n", 
            *block_number, SO_REGISTRY_SIZE);
    clean_end();
}

/* Received when the SO_SIM_SEC timer ends */
void sigalrm_handler(int signum)
{
    /* Reason (2) for termination: Expired SO_SIM_SEC timer */
    printf("Ending simulation: The execution lasted SO_SIM_SEC=%ld seconds.\n",
            conf[SO_SIM_SEC]);
    clean_end();
}

/* Received when a child process dies */
void sigchld_handler(int signum)
{
    /* Only user child processes can die */
    remaining_users--;
    if(remaining_users == 0){
        /* Reason (3) for termination: All the users stopped their execution */
        printf("Ending simulation: No more active users.\n");
        clean_end();
    }
}

#pragma endregion /* LIFETIME_AND_SIGNAL_HANDLERS */

/* -------------------- TERMINATION FUNCTIONS -------------------- */

#pragma region TERMINATION
/* TODO: Maybe add multiple shutdown functions, 1 for each IPC obj */
/* Kills all the child processes */
void send_kill_signals()
{
    int i = 0;

    /* For each PID in Users array, send signal */
    initReadFromShm(shmUsers);
    for(i = 0; i < conf[SO_USERS_NUM]; i++) {
        /* if the User is still alive, send the SIGINT signal */
        if(!kill(shmUsersArray[i].pid, 0)) {
            printf("Killing user %d...\n", shmUsersArray[i].pid);
            kill(shmUsersArray[i].pid, SIGINT);
        }
    }
    endReadFromShm(shmUsers);

    /* For each PID in Nodes array, send signal */
    initReadFromShm(shmNodes);
    for(i = 0; i < conf[SO_NODES_NUM]; i++) {
        /* if the Node is still alive, send the SIGINT signal */
        if(!kill(shmNodesArray[i].pid, 0)) {
            printf("killing node %d...\n", shmNodesArray[i].pid);
            kill(shmNodesArray[i].pid, SIGINT);
        }
    }
    endReadFromShm(shmNodes);
}

/* Clears all IPC objects, Memory free, exit */
void shutdown(int status) 
{
    int i = 0;

    /* detach the shmem for the last block number */
    shmdt((void *)block_number);
    if(block_number == (void *) -1){
        MSG_ERR("master.shutdown(): block_number, error while detaching "
                "the block_number shmem segment.");
	}

    /* detach the shmem for the shmUsersArray */
    shmdt((void *)shmUsersArray);
    if(shmUsersArray == (void *) -1){
        MSG_ERR("master.shutdown(): shmUsersArray, error while detaching "
                "the shmUsersArray shmem segment.");
	}

    /* detach the shmem for the shmNodesArray */
    shmdt((void *)shmNodesArray);
    if(shmNodesArray == (void *) -1){
        MSG_ERR("master.shutdown(): shmNodesArray, error while detaching "
                "the shmNodesArray shmem segment.");
	}

    /* detach the shmem for the Libro Mastro */
    shmdt((void *)libroMastroArray);
    if(libroMastroArray == (void *) -1){
        MSG_ERR("master.shutdown(): libroMastroArray, error while detaching "
                "the libroMastroArray shmem segment.");
	}

	/* Rimozione IPC */
	shmctl(shmUsers, IPC_RMID, NULL);
	shmctl(shmNodes, IPC_RMID, NULL);
	shmctl(shmLibroMastro, IPC_RMID, NULL);
	shmctl(shmBlockNumber, IPC_RMID, NULL);

	semctl(semUsers, 0, IPC_RMID, 0);
	semctl(semNodes, 0, IPC_RMID, 0);
	semctl(semLibroMastro, 0, IPC_RMID, 0);
	semctl(semSimulation, 0, IPC_RMID, 0);
	semctl(semBlockNumber, 0, IPC_RMID, 0);

    /* Removing Msg Queues */
    for(i=0; i < conf[SO_NODES_NUM]; i++)
	    msgctl(msgTransactions[i], IPC_RMID, NULL);
    free(msgTransactions);

    /* detach the shmem of the conf */
    shmdt((void *)conf);
    if(conf == (void *) -1){
        MSG_ERR("master.shutdown(): conf, error while detaching "
                "the configuration shmem segment.");
	}
	shmctl(shmConfig, IPC_RMID, NULL);

    exit(status);
}

/* Kills all the child processes still alive, prints the simulation stats,
    removes IPC Object and terminates the master process */
void clean_end()
{
    send_kill_signals();

    /* Attesa che tutti i figli siano conclusi */
    while (wait(NULL) != -1);

    print_stats(FORCE_PRINT_STATS);
    shutdown(EXIT_SUCCESS);
}

#pragma endregion /* TERMINATION */
