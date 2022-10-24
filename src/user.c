#define _GNU_SOURCE
#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include "common.h"


/* -------------------- PROTOTYPES -------------------- */

/* Initialization */
void initializeIPCs();
void attachSignalHandler();

/* Lifetime */
int createTransaction();
int getBilancio();

/* Signal Handlers */
void signalHandler();

/* Termination */
void shutdown();

/* -------------------- GLOBAL VARIABLES -------------------- */

/**** SHARED MEMORY IDs ****/
int shmConfig;        /* ID shmem configuration */
int shmNodes;         /* ID shmem nodes data */
int shmUsers;         /* ID shmem users data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int shmBlockNumber;   /* ID shmem libro mastro block number */

/**** SHARED MEMORY ATTACHED VARIABLES ****/
node *shmNodesArray;          /* Shmem Array of Node PIDs */
node *shmUsersArray;          /* Shmem Array of User PIDs */
block (*libroMastroArray)[SO_REGISTRY_SIZE];    /* Shmem Array of blocks */
unsigned int block_number;    /* Shmem number of the last block */
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
    int fails = 0;  /* used with SO_RETRY */
    int bilancio;   /*  */
    int status = 0; /*  */

    initializeIPCs();
    attachSignalHandler();

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
    return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

/*  */
void initializeIPCs()
{
    /* Creating the shared memory segment */
    shmConfig = shmget(SHM_ENV_KEY, 0,
                       0666);
    if (shmConfig == -1)
    {
        MSG_ERR("user: shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown();
        exit(EXIT_FAILURE);
    }

    conf = shmat(shmConfig, NULL, SHM_RDONLY);

    semUsers = semget(SEM_USER_KEY, 3, 0666);
    if (semUsers == -1)
    {
        MSG_ERR("user: semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
        shutdown();
        exit(EXIT_FAILURE);
    }

    semNodes = semget(SEM_NODE_KEY, 3, 0666);
    if (semNodes == -1)
    {
        MSG_ERR("user: semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
        shutdown();
        exit(EXIT_FAILURE);
    }


    semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, 0666);
    if (semLibroMastro == -1)
    {
        MSG_ERR("user: semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
        shutdown();
        exit(EXIT_FAILURE);
    }

    shmUsers = shmget(SHM_USER_KEY, sizeof(user) * conf[SO_USERS_NUM], 0666);
    if (shmUsers == -1)
    {
        MSG_ERR("user: shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
        shutdown();
        exit(EXIT_FAILURE);
    }

    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0666);
    if (shmNodes == -1)
    {
        MSG_ERR("user: shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes");
        shutdown();
        exit(EXIT_FAILURE);
    }

    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(block[SO_REGISTRY_SIZE]),
                            0666);
    if (shmLibroMastro == -1)
    {
        MSG_ERR("user: shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro");
        shutdown();
        exit(EXIT_FAILURE);
    }
}

/*  */
void attachSignalHandler()
{
    /*struct sigaction sa;
    sigset_t my_mask;
    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_NODEFER;
    sigemptyset(&my_mask);
    sa.sa_mask = my_mask;
    sigaction(SIGUSR1, &sa, NULL);*/
    set_handler(SIGUSR1, signalHandler);
    set_handler(SIGINT, sigint_handler);

    /*if(sigaction(SIGUSR1, *signalHandler, NULL) == SIG_ERR){
        MSG_ERR("user: signalHandler, error.");
        perror("signal SIGUSR1 error");
        shutdown();
        exit(EXIT_FAILURE);
    }*/
}

/* -------------------- LIFETIME FUNCTIONS -------------------- */

/*  */
int createTransaction(int bilancio)
{
    msgbuf t;
    struct timespec tempo;

    transaction newTr;  /* new transaction */
    int randomReceiverId; /* Random user */
    int randomReceiverPID; /* Random user */
    int randomNodeId;     /* Random node */
    int randomNodePID;     /* Random node */
    int randomQuantity; /* Random quantity for the transaction */
    int nodeReward;     /* Transaction reward */

    /* genera transazione */
    randomReceiverId = randomNum(0, conf[SO_USERS_NUM] - 1);
    randomNodeId = randomNum(0, conf[SO_NODES_NUM] - 1);
    randomQuantity = randomNum(2, bilancio);


    initReadFromShm(semUsers);
    shmUsersArray = shmat(shmUsers, NULL, SHM_RDONLY);
    randomReceiverPID = shmUsersArray[randomReceiverId].pid;
    shmdt((void *) shmUsersArray);
    endReadFromShm(semUsers);

    initReadFromShm(semNodes);
    shmNodesArray= shmat(shmNodes, NULL, SHM_RDONLY);
    randomNodePID= shmNodesArray[randomNodeId].pid;
    shmdt((void *) shmNodesArray);
    endReadFromShm(semNodes);

    msgTrans = msgget(ftok(FTOK_PATHNAME_NODE, randomNodePID), 0666);

    nodeReward = randomQuantity * conf[SO_REWARD] / 100;
    randomQuantity -= nodeReward;

    newTr.quantity = randomQuantity;
    newTr.receiver = randomReceiverPID;
    newTr.reward = nodeReward;
    newTr.sender = getpid();
    newTr.timestamp = (int)time(NULL);

    t.trans = newTr;
    t.mtype = 1;
    /* TODO : Invia al nodo = carica nel msgQueue del nodo */
    if(msgsnd(msgTrans, &t, sizeof(msgbuf), IPC_NOWAIT) == -1)
    {
        tempo.tv_sec = 0;
        tempo.tv_nsec = randomNum(conf[SO_MIN_TRANS_GEN_NSEC], conf[SO_MAX_TRANS_GEN_NSEC]);
        return 0;
    } else 
        return 1;

}

/*  */
int getBilancio()
{
    /* TODO: controllo transazioni fallite */
    /* calcolo bilancio - lettura with and readers solution */
    int bilancio = conf[SO_BUDGET_INIT];
    int k = 0, j = 0;
    initReadFromShm(semLibroMastro);
    libroMastroArray = shmat(shmLibroMastro, NULL, 0);
    if (libroMastroArray == (void *)-1)
    {
        perror("libroMastroArray shmat ");
        shutdown();
        exit(EXIT_FAILURE);
    }    
    for (k = 0; k < SO_REGISTRY_SIZE; k++)
    {
        for (j = 0; j < SO_BLOCK_SIZE; j++)
        {
            /* TODO: calcolo bilancio dev'essere calcolato anche in base alle transazioni in coda
                    contare anche i reward inviati ai nodi */
            /* printf("LibroMastro[%d][%d] vale %d\n", k, j, libroMastroArray[k][j].quantity); */

            if (libroMastroArray[k]->transBlock[j].receiver == getpid())
                bilancio += libroMastroArray[k]->transBlock[j].quantity;

            if (libroMastroArray[k]->transBlock[j].sender == getpid())
                bilancio -= libroMastroArray[k]->transBlock[j].quantity;
        }
    }
    shmdt((void *)libroMastroArray);
    if (libroMastroArray == (void *)-1)
        perror("libroMastroArray shmdt ");
    endReadFromShm(semLibroMastro);

    /*printf("[%d] ha bilancio uguale a : %d\n", getpid(), bilancio);*/

    return bilancio;
}

/* -------------------- SIGNAL HANDLERS -------------------- */

/*  */
void signalHandler(int signum)
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
    shutdown();
}

/* -------------------- TERMINATION FUNCTIONS -------------------- */

void shutdown()
{
    /* Rimozione IPC */
    shmctl(shmUsers, IPC_RMID, NULL);
    shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmConfig, IPC_RMID, NULL);

    semctl(semUsers, 0, IPC_RMID, 0);
    semctl(semNodes, 0, IPC_RMID, 0);
    semctl(semLibroMastro, 0, IPC_RMID, 0);
    /*semctl(semSimulation, 0, IPC_RMID, 0);*/

    /*msgctl(msgTransactions, IPC_RMID, NULL);*/
}

