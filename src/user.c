#define _GNU_SOURCE
#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include "common.h"
#include "bashprint.h"

/* -------------------- PROTOTYPES -------------------- */

/* Initialization */
void init();
void init_conf();
void init_sharedmem();
void init_semaphores();

/* Lifetime */
int createTransaction();
int getBilancio();

/* Signal Handlers */
void sigusr1_handler();
void sigint_handler();

/* Termination */
void shutdown(int status);

/* -------------------- GLOBAL VARIABLES -------------------- */

/**** SHARED MEMORY IDs ****/
int shmConfig;        /* ID shmem configuration */
int shmNodes;         /* ID shmem nodes data */
int shmUsers;         /* ID shmem users data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int shmBlockNumber;   /* ID shmem libro mastro block number */

/**** SHARED MEMORY ATTACHED VARIABLES ****/
node *shmNodesArray;          /* Shmem Array of Node PIDs */
user *shmUsersArray;          /* Shmem Array of User PIDs */
block *libroMastroArray;      /* Shmem Array of blocks */
unsigned int *block_number;   /* Shmem number of the last block */
unsigned long *conf;          /* Shmem Array of configuration values */

/**** MESSAGE QUEUE ID ****/
int msgTrans;        /* Message queue to send transactions */

/**** SEMAPHORE IDs ****/
int semNodes;        /* Semaphore for shmem access on the Array of Node PIDs */
int semUsers;        /* Semaphore for shmem access on the Array of User PIDs */
int semLibroMastro;  /* Semaphore for shmem access on the Libro Mastro */
int semBlockNumber;  /* Semaphore for the last block number */


int main(int argc, char **argv)
{
    /* TODO: check shmat, init, libromastroArray */
    int fails = 0;  /* used with SO_RETRY */
    int bilancio;   /*  */
    int status = 0; /*  */
    
    init();
	printf("user.main(): Ready to create transactions...\n");


    /* Starting user loop */
    while (fails < conf[SO_RETRY])
    {
        /*printf("\n creating bilancio ...");*/
        bilancio = getBilancio();
        if (bilancio >= 2) {
            if(createTransaction(bilancio) == 0)
                fails++;
        } else
            fails++;
    }
	printf("user.main(%d): Too many fails...\n", getpid());
    return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

/* Accessing all the required IPC objects, setting the signal handlers */
void init()
{
	init_conf();
	init_semaphores();
	init_sharedmem();
    
    /* Initializes seed for the random number generation */ 
    srand(time(NULL));

	/* Master wants to kill the node */
    set_handler(SIGUSR1, sigusr1_handler);
    set_handler(SIGINT,  sigint_handler);
}

/* Accessing the configuration shared memory segment in READ ONLY */
void init_conf()
{
    shmConfig = shmget( SHM_ENV_KEY, 
						sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
						SHM_RDONLY);
	if (shmConfig == -1){
		MSG_ERR("user.init(): shmConfig, error while getting the shared memory segment.");
        perror("\tshmConfig");
		shutdown(EXIT_FAILURE);
	}
    conf = shmat(shmConfig, NULL, 0);
}

/* Accessing the other shared memory segments and attaching */
void init_sharedmem()
{
    shmUsers = shmget(SHM_USER_KEY, sizeof(user) * conf[SO_USERS_NUM], 0666);
    if (shmUsers == -1)
    {
        MSG_ERR("user.init(): shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
        shutdown(EXIT_FAILURE);
    }
	shmUsersArray = (user *)shmat(shmUsers, NULL, 0);

    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0666);
    if (shmNodes == -1)
    {
        MSG_ERR("user.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes");
        shutdown(EXIT_FAILURE);
    }
	shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

    /* Accessing shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, sizeof(unsigned int), SHM_RDONLY);
    if (shmBlockNumber == -1){
		MSG_ERR("user.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber");
		shutdown(EXIT_FAILURE); 
	}
    block_number = (unsigned int *)shmat(shmBlockNumber, NULL, 0);

    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(block) * SO_REGISTRY_SIZE, 0666);
    if (shmLibroMastro == -1)
    {
        MSG_ERR("user.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro");
        shutdown(EXIT_FAILURE);
    }
	libroMastroArray = (block *)shmat(shmLibroMastro, NULL, 0);
}

/* Accessing to the semaphores for the shared memory */
void init_semaphores()
{
    semUsers = semget(SEM_USER_KEY, 3, 0666);
    if (semUsers == -1)
    {
        MSG_ERR("user.init(): semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
        shutdown(EXIT_FAILURE);
    }

    semNodes = semget(SEM_NODE_KEY, 3, 0666);
    if (semNodes == -1)
    {
        MSG_ERR("user.init(): semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
        shutdown(EXIT_FAILURE);
    }

    semBlockNumber = semget(SEM_BLOCK_NUMBER, 1, 0666);
	if(semBlockNumber == -1){
		MSG_ERR("user.init(): semBlockNumber, error while getting the semaphore.");
        perror("\tsemBlockNumber");
		shutdown(EXIT_FAILURE);
	}

    semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, 0666);
    if (semLibroMastro == -1)
    {
        MSG_ERR("user.init(): semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
        shutdown(EXIT_FAILURE);
    }
}

/* -------------------- LIFETIME FUNCTIONS -------------------- */

/*  */
int createTransaction(int bilancio)
{
    msgbuf msg;
    struct timespec tempo;

    transaction newTr;  /* new transaction */
    int randomReceiverId; /* Random user */
    int randomReceiverPID; /* Random user */
    int randomNodeId;     /* Random node */
    int randomNodePID;     /* Random node */
    int randomQuantity; /* Random quantity for the transaction */
    int nodeReward;     /* Transaction reward */

    /* genera transazione */
    initReadFromShm(semUsers);
    do{
        randomReceiverId = randomNum(0, conf[SO_USERS_NUM] - 1);
        randomReceiverPID = shmUsersArray[randomReceiverId].pid;
    }
    while(randomReceiverPID == getpid());
    endReadFromShm(semUsers);

    randomNodeId = randomNum(0, conf[SO_NODES_NUM] - 1);
    randomQuantity = randomNum(2, bilancio);

    initReadFromShm(semNodes);
    randomNodePID = shmNodesArray[randomNodeId].pid;
    endReadFromShm(semNodes);

    msgTrans = msgget(ftok(FTOK_PATHNAME_NODE, randomNodePID), 0666);

    nodeReward = randomQuantity * conf[SO_REWARD] / 100;
    randomQuantity -= nodeReward;

    newTr.quantity = randomQuantity;
    newTr.receiver = randomReceiverPID;
    newTr.reward = nodeReward;
    newTr.sender = getpid();
    newTr.timestamp = (int)time(NULL);

    msg.trans = newTr;
    msg.mtype = 1;
    /* TODO : Invia al nodo = carica nel msgQueue del nodo */
    if(msgsnd(msgTrans, &msg, sizeof(msgbuf), IPC_NOWAIT) < 0)
    {
        return 0;
    } else {
        tempo.tv_sec = 0;
        tempo.tv_nsec = randomNum(conf[SO_MIN_TRANS_GEN_NSEC], conf[SO_MAX_TRANS_GEN_NSEC]);
        nanosleep(&tempo, &tempo);
    }
    printf("Transazione inviata con successo al nodo: %d\n", randomNodePID);
    return 1;
}

/*  */
int getBilancio()
{
    /* calcolo bilancio - lettura with and readers solution */
    int bilancio = conf[SO_BUDGET_INIT];
    int k = 0, j = 0; 
    initReadFromShm(semBlockNumber);
    initReadFromShm(semLibroMastro);
    for (k = 0; k < *block_number; k++)
    {
        for (j = 0; j < SO_BLOCK_SIZE; j++)
        {
            /* TODO: calcolo bilancio dev'essere calcolato anche in base alle transazioni in coda
                    contare anche i reward inviati ai nodi */

            if (libroMastroArray[k].transBlock[j].receiver == getpid())
                bilancio += libroMastroArray[k].transBlock[j].quantity;

            if (libroMastroArray[k].transBlock[j].sender == getpid())
                bilancio -= libroMastroArray[k].transBlock[j].quantity;
        }
    }
    endReadFromShm(semLibroMastro);
    endReadFromShm(semBlockNumber);

    initWriteInShm(semNodes);
    k = 0;
    while(shmUsersArray[k].pid != getpid())
        k++;
    shmUsersArray[k].budget = bilancio;
    endWriteInShm(semNodes);

    return bilancio;
}

/* -------------------- SIGNAL HANDLERS -------------------- */

/*  */
void sigusr1_handler(int signum)
{
    /*int bilancio = getBilancio();*/
    printf("Segnale ricevuto - creazione transazione\n");
    /*if(bilancio > 0)
        createTransaction(bilancio);*/

    /* Riassegnazione dell'handler */
    /*attachSignalHandler();
    printf("Routine completa\n");*/
    fflush(stdout);
}

/* received when master process wants to kill the user */
void sigint_handler(int signum)
{
    getBilancio();
    shutdown(EXIT_SUCCESS);
}

/* -------------------- TERMINATION FUNCTIONS -------------------- */

void shutdown(int status)
{
    /* Rimozione IPC */
    /* detach the shmem for the Users array */
    shmdt((void *)shmUsersArray);
    if(shmUsersArray == (void *) -1){
        MSG_ERR("user.shutdown(): shmUsersArray, error while detaching "
                "the shmUsersArray shmem segment.");
	}

    /* detach the shmem for the Nodes array */
    shmdt((void *)shmNodesArray);
    if(shmNodesArray == (void *) -1){
        MSG_ERR("user.shutdown(): shmNodesArray, error while detaching "
                "the shmNodesArray shmem segment.");
	}

	/* detach the shmem for the libro mastro */
    shmdt((void *)libroMastroArray);
    if(libroMastroArray == (void *) -1){
        MSG_ERR("user.shutdown(): libroMastroArray, error while detaching "
                "the libroMastroArray shmem segment.");
	}

    /* detach the shmem for the last block number */
    shmdt((void *)block_number);
    if(block_number == (void *) -1){
        MSG_ERR("user.shutdown(): block_number, error while detaching "
                "the block_number shmem segment.");
	}

	/* detach the shmem for the configuration */
    shmdt((void *)conf);
    if(conf == (void *) -1){
        MSG_ERR("user.shutdown(): conf, error while detaching "
                "the conf shmem segment.");
	}
    shmctl(shmUsers, IPC_RMID, NULL);
    shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmConfig, IPC_RMID, NULL);
    shmctl(shmBlockNumber, IPC_RMID, NULL);

    semctl(semUsers, 0, IPC_RMID, 0);
    semctl(semNodes, 0, IPC_RMID, 0);
    semctl(semLibroMastro, 0, IPC_RMID, 0);
	semctl(semBlockNumber, 0, IPC_RMID, 0);
    /*semctl(semSimulation, 0, IPC_RMID, 0);*/

    /*msgctl(msgTransactions, IPC_RMID, NULL);*/
    exit(status);
}

