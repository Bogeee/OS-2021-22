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
const char conf_names[N_RUNTIME_CONF_VALUES][22+1] = {
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
void wr_ids_to_file(char mode);
void init_sighandlers();
void get_configuration(unsigned long *conf);
void users_generation();
void nodes_generation();

/* Lifetime */
void print_stats(int force_print);
void print_all_users();
void print_most_relevant_users();
void print_all_nodes();
void print_most_relevant_nodes();

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
int is_terminating;  /* Boolean used to detect the termination */
int nodes_generated; /* Boolean to 1 if the nodes were generated */
int users_generated; /* Boolean to 1 if the users were generated */
int term_reason;     /* Defines reason of termination */
int early_deaths;    /* Number of early death users */

int main (int argc, char ** argv)
{
    /* To print the stats every second */
    struct timespec req = {1, 0};
    struct timespec rem;
    int nanofail = 0;

    /* (1): Get simulation configuration, initialize IPC Objects */
    init_sighandlers();
    init();

    /* (2): Generate child processes */
    nodes_generation();
    nodes_generated = 1;
    /* For statistical purposes */
    remaining_nodes = conf[SO_NODES_NUM];

    /* Write msgqueue IDs */
    wr_ids_to_file('a');
    
    users_generation();
    users_generated = 1;
    /* When this integer reaches 0, the simulation must end */
    remaining_users = conf[SO_USERS_NUM];

    /* 
     * (3): Signal handling, signals to the Master are used to determine
     *      termination conditions.
     *      - Print stats every second. 
     */

    alarm(conf[SO_SIM_SEC]);
    while (1){
        if(nanofail){
            req = rem;
            nanofail = 0;
        } else {
            req.tv_sec = 1;
            req.tv_nsec = 0;
        }
        if(nanosleep(&req, &rem) < 0){
            nanofail = 1;
        } else {
        	block_signals(4, SIGINT, SIGTERM, SIGUSR1, SIGALRM);
            print_stats(PRINT_USEFUL_STATS);
        	unblock_signals(4, SIGINT, SIGTERM, SIGUSR1, SIGALRM);
        }
    }

    /*
     * (4): Clear IPC object, Memory Free and Termination are managed by
     *      the termination functions.
     */ 

	return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

#pragma region INITIALIZATION

/* Wrapper function that calls the other initialization functions  */
void init()
{
    /* Simulation is starting */
    is_terminating = 0;
    nodes_generated = 0;
    users_generated = 0;
    term_reason = 0;
    early_deaths = 0;

    /* Setting IPC IDs to -1 */
    semUsers = -1;
    semNodes = -1;
    semLibroMastro = -1;
    semSimulation = -1;
    semBlockNumber = -1;
    shmConfig = -1;
    shmUsers = -1;
    shmNodes = -1;
    shmLibroMastro = -1;
    shmBlockNumber = -1;
    
    init_conf();
    init_semaphores();
	init_sharedmem();

    /* Write shmem and semaphore IDs */
    wr_ids_to_file('w');
    /* 
     * Message queues are created in nodes_generation(),
     * this is just the array of Msg queue IDs
     */
    msgTransactions = (int*) malloc(conf[SO_NODES_NUM] * sizeof(int));
    if(msgTransactions == NULL){
        MSG_ERR("master.init(): msgTransactions, error while allocating "
                "memory for msgqueue IDs");
        perror("\tmsgTransactions: ");
        shutdown(EXIT_FAILURE);
    }
    
    /* Initializes seed for the random number generation */ 
    srand(getpid()+getppid());
}

/* Gets the configuration and write it in shared memory */
void init_conf()
{
    /* Creating the shared memory segment */
    shmConfig = shmget( SHM_ENV_KEY, 
                        sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
                        IPC_CREAT | IPC_EXCL | 0600);
    if (shmConfig == -1){
        MSG_ERR("master.init(): shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown(EXIT_FAILURE);
    }

    conf = shmat(shmConfig, NULL, 0);
    if(conf == (void *) -1){
        MSG_ERR("master.init(): shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown(EXIT_FAILURE);
    }

    /* Gets conf from Env variables, Writes configuration to shared memory */
    get_configuration(conf);
}

/* Creates the semaphores and initializes them */
void init_semaphores()
{
    semUsers = semget(SEM_USER_KEY, 3, IPC_CREAT | 0600);
    if(semUsers == -1){
		MSG_ERR("master.init(): semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
		shutdown(EXIT_FAILURE);
	}

	semNodes = semget(SEM_NODE_KEY, 3, IPC_CREAT | 0600);
    if(semNodes == -1){
		MSG_ERR("master.init(): semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, IPC_CREAT | 0600);
    if(semLibroMastro == -1){
		MSG_ERR("master.init(): semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}

    semBlockNumber = semget(SEM_BLOCK_NUMBER, 3, IPC_CREAT | 0600);
    if(semBlockNumber == -1){
		MSG_ERR("master.init(): semBlockNumber, error while creating the semaphore.");
        perror("\tsemBlockNumber");
		shutdown(EXIT_FAILURE);
	}

    semSimulation = semget(SEM_SIM_KEY, 1, IPC_CREAT | 0600);
    if(semSimulation == -1){
		MSG_ERR("master.init(): semSimulation, error while creating the semaphore.");
        perror("\tsemSimulazione");
		shutdown(EXIT_FAILURE);
	}

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
    initSemAvailable(semBlockNumber, 1);
    initSemInUse(semBlockNumber, 2);
}

/* Creates the shmem segments */
void init_sharedmem()
{
    /* Creating shmem segment for Users */
    shmUsers = shmget(  SHM_USER_KEY, 
                        sizeof(user) * conf[SO_USERS_NUM], 
                        IPC_CREAT | IPC_EXCL | 0600);
    if (shmUsers == -1){
		MSG_ERR("master.init(): shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
		shutdown(EXIT_FAILURE);
	}
    shmUsersArray = (user *)shmat(shmUsers, NULL, 0);

    /* Creating shmem segment for Nodes */
    shmNodes = shmget(  SHM_NODE_KEY, 
                        sizeof(node) * conf[SO_NODES_NUM], 
                        IPC_CREAT | IPC_EXCL | 0600);
    if (shmNodes == -1){
		MSG_ERR("master.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes");
		shutdown(EXIT_FAILURE);
	}
    shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

    /* Creating shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, 
                            sizeof(block) * SO_REGISTRY_SIZE,
                            IPC_CREAT | IPC_EXCL | 0600);
    if (shmLibroMastro == -1){
		MSG_ERR("master.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tshmLibroMastro");
		shutdown(EXIT_FAILURE);
	}
    libroMastroArray = (block *)shmat(shmLibroMastro, NULL, 0);

    /* Creating shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, 
                            sizeof(unsigned int), 
                            IPC_CREAT | IPC_EXCL | 0600);
    if (shmBlockNumber == -1){
		MSG_ERR("master.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber");
		shutdown(EXIT_FAILURE); 
	}
    /* Block number initialization */
    block_number = (unsigned int *)shmat(shmBlockNumber, NULL, 0);
    *block_number = 0;
}

/* Write ipc ids to file */
void wr_ids_to_file(char mode)
{
    FILE *fp_ids;
    int i = 0;

    if(mode == 'w')
        fp_ids = fopen(IPC_IDS_FILENAME, "w");
    else
        fp_ids = fopen(IPC_IDS_FILENAME, "a");

    if(mode == 'w'){
        fprintf(fp_ids, "SEMAPHORES\n");
        fprintf(fp_ids, "\tsemUsers: %d\n", semUsers);
        fprintf(fp_ids, "\tsemNodes: %d\n", semNodes);
        fprintf(fp_ids, "\tsemLibroMastro: %d\n", semLibroMastro);
        fprintf(fp_ids, "\tsemBlockNumber: %d\n", semBlockNumber);
        fprintf(fp_ids, "\tsemSimulation: %d\n\n", semSimulation);
        fprintf(fp_ids, "SHARED MEMORY\n");
        fprintf(fp_ids, "\tshmConfig: %d\n", shmConfig);
        fprintf(fp_ids, "\tshmUsers: %d\n", shmUsers);
        fprintf(fp_ids, "\tshmNodes: %d\n", shmNodes);
        fprintf(fp_ids, "\tshmLibroMastro: %d\n", shmLibroMastro);
        fprintf(fp_ids, "\tshmBlockNumber: %d\n\n", shmBlockNumber);
        fprintf(fp_ids, "MESSAGE QUEUES\n");
    } else {
        for(i = 0; i < conf[SO_NODES_NUM]; i++)
            fprintf(fp_ids, "\tmsgQueue(%d): %d\n", i, msgTransactions[i]);
    }

    fclose(fp_ids);
}

/* Setting the signal handlers */
void init_sighandlers()
{
	set_handler(SIGTERM, sigterm_handler);
	set_handler(SIGINT,  sigterm_handler);
	set_handler(SIGALRM, sigalrm_handler);
	set_handler(SIGCHLD, sigchld_handler);
	set_handler(SIGUSR1, sigusr1_handler);
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
	shutdown(EXIT_FAILURE);
#endif
#ifndef SO_REGISTRY_SIZE
	MSG_ERR("SO_REGISTRY_SIZE is not defined!");
	shutdown(EXIT_FAILURE);
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
				shutdown(EXIT_FAILURE);
			}
			/* check valid number */
			if(i == SO_MAX_TRANS_GEN_NSEC && conf[SO_MAX_TRANS_GEN_NSEC] 
                < conf[SO_MIN_TRANS_GEN_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_GEN_NSEC is lower than SO_MIN_TRANS_GEN_NSEC!");
				shutdown(EXIT_FAILURE);
			} else if(i == SO_MAX_TRANS_PROC_NSEC 
                        && conf[SO_MAX_TRANS_PROC_NSEC] 
                        < conf[SO_MIN_TRANS_PROC_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_PROC_NSEC is lower than SO_MIN_TRANS_PROC_NSEC!");
				shutdown(EXIT_FAILURE);
			} else if(i == SO_TP_SIZE && conf[SO_TP_SIZE] <= SO_BLOCK_SIZE) {
				MSG_ERR("SO_TP_SIZE is not bigger than SO_BLOCK_SIZE!");
				shutdown(EXIT_FAILURE);
			} else if(i == SO_REWARD && (conf[SO_REWARD] < 0 
                        || conf[SO_REWARD] > 100)) {
				MSG_ERR("SO_REWARD is out range [0-100]!");
				shutdown(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, 
                    "[%sERROR%s] Undefined environment variable %s. Make sure to load env. variables first!\n"
                    "        Example: source cfg/custom.cfg\n",
                    COLOR_RED, COLOR_FLUSH, conf_names[i]);
			shutdown(EXIT_FAILURE);
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
    struct timespec t;
    t.tv_sec = 1;
    t.tv_nsec = 0;

    for (i = 0; i < conf[SO_USERS_NUM]; i++)
    {
        child_pid = fork();
        switch (child_pid)
        {
        case -1:
            MSG_ERR("master.users_generation(): error while generating users.");
            perror("\tFork user error ");
            shutdown(EXIT_FAILURE);
            break;
        case 0:
            /* Child branch */

            /* Shmem write */
            block_signals(2, SIGINT, SIGTERM);
            initWriteInShm(semUsers);
            if (shmUsersArray[i].pid == 0)
            {
                shmUsersArray[i].pid = getpid();
                shmUsersArray[i].budget = conf[SO_BUDGET_INIT];
                shmUsersArray[i].alive = 1;
            }
            endWriteInShm(semUsers);
            unblock_signals(2, SIGINT, SIGTERM);

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
    struct msqid_ds msg_params;     /* Used to check system limits */
	msglen_t msg_max_size_no_root;  /* System max msgqueue size */
    FILE *fp_ids;                   /* Write msgqueues to file */
    int test_msgqueue = -1;

	test_msgqueue = msgget(ftok("./bin/master", getpid()),
                           IPC_CREAT | IPC_EXCL | 0600);
    if(test_msgqueue == -1){
        MSG_ERR("master.nodes_generation(): test_msgqueue, error while creating the message queue.");
        perror("\ttest_msgqueue ");
        shutdown(EXIT_FAILURE);
    }

    if(msgctl(test_msgqueue, IPC_STAT, &msg_params) == 0){
	    msg_max_size_no_root = msg_params.msg_qbytes;
    } else {
        MSG_ERR("master.nodes_generation(): test_msgqueue, error while getting the system msgqueue settings.");
        perror("\ttest_msgqueue ");
        shutdown(EXIT_FAILURE);
    }

	if((sizeof(msgbuf) * conf[SO_TP_SIZE]) > msg_max_size_no_root){
		MSG_ERR("master.nodes_generation(): msg_queue_size, the transaction "
                "pool is bigger than the maximum msgqueue size.");
		MSG_INFO2("\tYou should change the MSGMNB kernel info with root privileges.");
        msgctl(test_msgqueue, IPC_RMID, NULL);
		shutdown(EXIT_FAILURE);
	} 
    msgctl(test_msgqueue, IPC_RMID, NULL);

    for (i = 0; i < conf[SO_NODES_NUM]; i++){
        child_pid = fork();
        switch (child_pid){
        case -1:
            MSG_ERR("master.nodes_generation(): error while generating nodes.");
            perror("\tFork node error ");
            shutdown(EXIT_FAILURE);
            break;
        case 0:
            /* child branch */
            child_pid = getpid();

            /* Shmem write */
            block_signals(2, SIGINT, SIGTERM);
            initWriteInShm(semNodes);
            if (shmNodesArray[i].pid == 0)
            {
                shmNodesArray[i].pid = child_pid;
                shmNodesArray[i].reward = 0;
                shmNodesArray[i].unproc_trans = 0;
            }
            endWriteInShm(semNodes);
            unblock_signals(2, SIGINT, SIGTERM);

            msgTransactions[i] = msgget(ftok(FTOK_PATHNAME_NODE, child_pid), 
                                        IPC_CREAT | IPC_EXCL | 0600);
            if(msgTransactions[i] == -1){
                MSG_ERR("master.nodes_generation(): msgTransactions, error while creating the message queue.");
                perror("\tmsgTransactions ");
                fclose(fp_ids);
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

/* Prints the useful stats in lifetime, Prints all the info before exit() */
void print_stats(int force_print)
{
    int i = 0, j = 0;
    int cond = (users_generated && nodes_generated);
    FILE *fp;
    
    if(force_print && cond){
        print_all_users();
        print_all_nodes();
        printf("Users died too early: [%d/%d]\n", 
               early_deaths, conf[SO_USERS_NUM]);

        initReadFromShm(semBlockNumber);
        printf("# of blocks: %d\n", *block_number);
        endReadFromShm(semBlockNumber);

        if(term_reason == 1)
            printf("Simulation ended: The blockchain is full -> [%d/%ld]\n",
                   *block_number, SO_REGISTRY_SIZE);
        else if(term_reason == 2)
            printf("Simulation ended: The execution lasted SO_SIM_SEC=%ld seconds.\n",
                   conf[SO_SIM_SEC]);
        else if(term_reason == 3)
            printf("Simulation ended: No more active users.\n");
        else if(term_reason == 4)
            printf("Simulation ended: Interrupt signal received.\n");

        /* Blockchain print to file */
        fp = fopen("./out/blockchain", "w");
        initReadFromShm(semLibroMastro);
        initReadFromShm(semBlockNumber);
        fprintf(fp, "\n\n===============BLOCKCHAIN==============\n");
        fprintf(fp, "# of blocks: %d\n", *block_number);

        for(i = 0; i < *block_number; i++){
            fprintf(fp, "Block #%d:\n", i);
            for(j = 0; j < SO_BLOCK_SIZE; j++){
                fprintf(fp, "\tTransaction #%d: t=%d.%d\t snd=%d\t rcv=%d\t qty=%d\t rwd=%d\n",
                        j, libroMastroArray[i].transBlock[j].timestamp.tv_sec,
                        libroMastroArray[i].transBlock[j].timestamp.tv_nsec,
                        libroMastroArray[i].transBlock[j].sender,
                        libroMastroArray[i].transBlock[j].receiver,
                        libroMastroArray[i].transBlock[j].quantity,
                        libroMastroArray[i].transBlock[j].reward);
            }
        }
        endReadFromShm(semBlockNumber);
        endReadFromShm(semLibroMastro);
        fclose(fp);
    }
    else if(cond){
        printf("\n\n===============ACTIVE==============\n");
        printf("\tActive users: [%d/%d]\n", remaining_users, 
               conf[SO_USERS_NUM]);
        printf("\tActive nodes: [%d/%d]\n\n", remaining_nodes, 
               conf[SO_NODES_NUM]);

        if(conf[SO_USERS_NUM] < 6){
            print_all_users();
        } else {
            print_most_relevant_users();
        }

        if(conf[SO_NODES_NUM] < 6){
            print_all_nodes();
        } else {
            print_most_relevant_nodes();
        }
    }
}

/* Prints all users' info */
void print_all_users()
{
    int i = 0;

    initReadFromShm(semUsers);
    printf("\n\n===============USERS==============\n");
    for(i = 0; i < conf[SO_USERS_NUM]; i++){
        printf("\tPID: %d\n", shmUsersArray[i].pid);
        printf("\tBudget: %d\n\n", shmUsersArray[i].budget);
    }
    endReadFromShm(semUsers);
}

/* Prints the richest and poorest users */
void print_most_relevant_users()
{
    int i = 0;
    pid_t pid_min;
    pid_t pid_max;
    int min = INT_MAX;
    int max = INT_MIN;

    initReadFromShm(semUsers);
    printf("\n\n===============RICHEST & POOREST USERS==============\n");
    for(i = 0; i < conf[SO_USERS_NUM]; i++){
        if(shmUsersArray[i].budget < min){
            min = shmUsersArray[i].budget;
            pid_min = shmUsersArray[i].pid;
        }
        if(shmUsersArray[i].budget > max){
            max = shmUsersArray[i].budget;
            pid_max = shmUsersArray[i].pid;
        }
    }
    endReadFromShm(semUsers);

    printf("Poorest:\n");
    printf("\tPID: %d\n", pid_min);
    printf("\tBudget: %d\n\n", min);
    printf("Richest:\n");
    printf("\tPID: %d\n", pid_max);
    printf("\tBudget: %d\n\n", max);
}

/* Prints all nodes' info */
void print_all_nodes()
{
    int i = 0;

    initReadFromShm(semNodes);
    printf("\n\n===============NODES==============\n");
    for(i = 0; i < conf[SO_NODES_NUM]; i++){
        printf("\tPID: %d\n", shmNodesArray[i].pid);
        printf("\tReward: %d\n", shmNodesArray[i].reward);
        if(is_terminating)
            printf("\tUnprocessed transactions: %d\n\n", 
                   shmNodesArray[i].unproc_trans);
        else
            printf("\n");
    }
    endReadFromShm(semNodes);
}

/* Prints the richest and poorest nodes */
void print_most_relevant_nodes()
{
    int i = 0;
    pid_t pid_min;
    pid_t pid_max;
    int min = INT_MAX;
    int max = INT_MIN;

    initReadFromShm(semNodes);
    printf("\n\n===============RICHEST & POOREST NODES==============\n");
    for(i = 0; i < conf[SO_NODES_NUM]; i++){
        if(shmNodesArray[i].reward < min){
            min = shmNodesArray[i].reward;
            pid_min = shmNodesArray[i].pid;
        }
        if(shmNodesArray[i].reward > max){
            max = shmNodesArray[i].reward;
            pid_max = shmNodesArray[i].pid;
        }
    }
    endReadFromShm(semNodes);

    printf("Poorest:\n");
    printf("\tPID: %d\n", pid_min);
    printf("\tReward: %d\n\n", min);
    printf("Richest:\n");
    printf("\tPID: %d\n", pid_max);
    printf("\tReward: %d\n\n", max);
}

/* -------------------- SIGNAL HANDLERS -------------------- */
/* SIGINT and SIGTERM handlers */
void sigterm_handler(int signum) 
{
    /* Reason (4) for termination: Interrupt signal */
    if(!is_terminating){
        printf("[INFO] Ricevuto il segnale %s, arresto la simulazione\n",
                strsignal(signum));
        is_terminating = 1;
        term_reason = 4;
        clean_end();
    }
}

/* Received when the blockchain is full */
void sigusr1_handler(int signum)
{
    /* Reason (1) for termination: The blockchain is full */
    if(!is_terminating){
        is_terminating = 1;
        term_reason = 1;
        clean_end();
    }
}

/* Received when the SO_SIM_SEC timer ends */
void sigalrm_handler(int signum)
{
    /* Reason (2) for termination: Expired SO_SIM_SEC timer */
    if(!is_terminating){
        is_terminating = 1;
        term_reason = 2;
        clean_end();
    }
}

/* Received when a child process dies */
void sigchld_handler(int signum)
{
    /* Only user child processes can die before the end */
    remaining_users--;
    if(!is_terminating){
        early_deaths++;
    }
    if(remaining_users == 0){
        /* Reason (3) for termination: All the users stopped their execution */
        if(!is_terminating){
            is_terminating = 1;
            term_reason = 3;
            clean_end();
        }
    }
}

#pragma endregion /* LIFETIME_AND_SIGNAL_HANDLERS */

/* -------------------- TERMINATION FUNCTIONS -------------------- */

#pragma region TERMINATION

/* Kills all the child processes */
void send_kill_signals()
{
    int i = 0;

    /* For each PID in Users array, send signal */
    if(users_generated){
        initReadFromShm(semUsers);
        for(i = 0; i < conf[SO_USERS_NUM]; i++) {
            /* if the User is still alive, send the SIGINT signal */
            if(!kill(shmUsersArray[i].pid, 0)) {
#ifdef DEBUG
                printf("[INFO] Killing user %d...\n", shmUsersArray[i].pid);
#endif
                kill(shmUsersArray[i].pid, SIGINT);
            }
        }
        endReadFromShm(semUsers);
    }

    /* For each PID in Nodes array, send signal */
    if(nodes_generated){
        initReadFromShm(semNodes);
        for(i = 0; i < conf[SO_NODES_NUM]; i++) {
            /* if the Node is still alive, send the SIGINT signal */
            if(!kill(shmNodesArray[i].pid, 0)) {
#ifdef DEBUG
                printf("[INFO] killing node %d...\n", shmNodesArray[i].pid);
#endif
                kill(shmNodesArray[i].pid, SIGINT);
            }
        }
        endReadFromShm(semNodes);
    }
}

/* Clears all IPC objects, Memory free, exit */
void shutdown(int status) 
{
    int i = 0;

    /* detach the shmem for the last block number */
    if(shmBlockNumber != -1 && shmdt((void *)block_number) == -1){
        MSG_ERR("master.shutdown(): block_number, error while detaching "
                "the block_number shmem segment.");
        perror("\tblock_number shmdt ");
	}

    /* detach the shmem for the shmUsersArray */
    if(shmUsers != -1 && shmdt((void *)shmUsersArray) == -1){
        MSG_ERR("master.shutdown(): shmUsersArray, error while detaching "
                "the shmUsersArray shmem segment.");
        perror("\tshmUsersArray shmdt ");
	}

    /* detach the shmem for the shmNodesArray */
    if(shmNodes != -1 && shmdt((void *)shmNodesArray) == -1){
        MSG_ERR("master.shutdown(): shmNodesArray, error while detaching "
                "the shmNodesArray shmem segment.");
        perror("\tshmNodesArray shmdt ");
	}

    /* detach the shmem for the Libro Mastro */
    if(shmLibroMastro != -1 && shmdt((void *)libroMastroArray) == -1){
        MSG_ERR("master.shutdown(): libroMastroArray, error while detaching "
                "the libroMastroArray shmem segment.");
        perror("\tlibroMastroArray shmdt ");
	}

	/* Removing shmem segments */
	shmctl(shmUsers, IPC_RMID, NULL);
	shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmBlockNumber, IPC_RMID, NULL);

	/* Removing semaphores */
	semctl(semUsers, 0, IPC_RMID, 0);
    semctl(semNodes, 0, IPC_RMID, 0);
    semctl(semLibroMastro, 0, IPC_RMID, 0);
    semctl(semSimulation, 0, IPC_RMID, 0);
    semctl(semBlockNumber, 0, IPC_RMID, 0);

    /* Removing Msg Queues */
    if(nodes_generated){
        for(i=0; i < conf[SO_NODES_NUM]; i++)
            msgctl(msgTransactions[i], IPC_RMID, NULL);
        free(msgTransactions);
    }

    /* detach the shmem of the conf */
    if(shmdt((void *)conf) == -1){
        MSG_ERR("master.shutdown(): conf, error while detaching "
                "the conf shmem segment.");
        perror("\tconf shmdt ");
	}
    shmctl(shmConfig, IPC_RMID, NULL);

    exit(status);
}

/* Kills all the child processes still alive, prints the simulation stats,
    removes IPC Object and terminates the master process */
void clean_end()
{
    send_kill_signals();

    print_stats(FORCE_PRINT_STATS);
    shutdown(EXIT_SUCCESS);
}

#pragma endregion /* TERMINATION */
