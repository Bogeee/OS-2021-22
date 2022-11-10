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
void getBilancio();
void addToPendingList(transaction tr);
void printPendingList();
void removeFromPendingList(transaction tr);
void freePendingList();

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
int semSimulation;   /* Semaphore for the simulation */

int bilancio;    /* User's budget */
struct pendingTr *pendingList; /* List of unprocessed transactions */

int main(int argc, char **argv)
{
    int fails = 0;  /* used with SO_RETRY */
    
    init();

#ifdef DEBUG
	printf("[INFO] user.main(%d): Ready to create transactions...\n", getpid());
#endif

    /* Starting user loop */
    while (fails < conf[SO_RETRY])
    {
        /*printf("\n creating bilancio ...");*/
        getBilancio();
        if (bilancio >= 2) {
            if(createTransaction() == 0)
                fails++;
        } else
            fails++;
    }
#ifdef DEBUG
    getBilancio();
	printf("[INFO] user.main(%d): Too many fails...%d\n", getpid(), bilancio);
#endif
    return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

/* Accessing all the required IPC objects, setting the signal handlers */
void init()
{
    struct sembuf s;
    s.sem_num = 0;
    s.sem_op = 0;
    s.sem_flg = 0;
    
	init_conf();
	init_semaphores();
	init_sharedmem();

    /* Initializes seed for the random number generation */ 
    srand(getpid()+getppid());

    /* Initializes the User's budget */
    bilancio = conf[SO_BUDGET_INIT];

    /* Create pending transactions list */
    pendingList = NULL;

	/* Master wants to kill the node */
    set_handler(SIGUSR1, sigusr1_handler);
    set_handler(SIGINT,  sigint_handler);

	/* Waiting that the other nodes are ready and active */
	reserveSem(semSimulation, 0);
    if(semop(semSimulation, &s, 1) == -1){
        MSG_ERR("user.init(): error while waiting for zero on semSimulation.");
        perror("\tsemSimulation: ");
    }
}

/* Accessing the configuration shared memory segment in READ ONLY */
void init_conf()
{
    shmConfig = shmget( SHM_ENV_KEY, 
						sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
						SHM_RDONLY);
	if (shmConfig == -1){
		MSG_ERR("user.init(): shmConfig, error while getting the shared memory segment.");
        perror("\tshmConfig ");
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
        perror("\tshmUsers ");
        shutdown(EXIT_FAILURE);
    }
	shmUsersArray = (user *)shmat(shmUsers, NULL, 0);

    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0666);
    if (shmNodes == -1)
    {
        MSG_ERR("user.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes ");
        shutdown(EXIT_FAILURE);
    }
	shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

    /* Accessing shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, sizeof(unsigned int), SHM_RDONLY);
    if (shmBlockNumber == -1){
		MSG_ERR("user.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber ");
		shutdown(EXIT_FAILURE); 
	}
    block_number = (unsigned int *)shmat(shmBlockNumber, NULL, 0);

    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(block) * SO_REGISTRY_SIZE, SHM_RDONLY | 0666);
    if (shmLibroMastro == -1)
    {
        MSG_ERR("user.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro ");
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
        perror("\tsemUsers ");
        shutdown(EXIT_FAILURE);
    }

    semNodes = semget(SEM_NODE_KEY, 3, 0666);
    if (semNodes == -1)
    {
        MSG_ERR("user.init(): semNodes, error while getting the semaphore.");
        perror("\tsemNodes ");
        shutdown(EXIT_FAILURE);
    }

    semBlockNumber = semget(SEM_BLOCK_NUMBER, 1, 0666);
	if(semBlockNumber == -1){
		MSG_ERR("user.init(): semBlockNumber, error while getting the semaphore.");
        perror("\tsemBlockNumber ");
		shutdown(EXIT_FAILURE);
	}

    semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, 0666);
    if (semLibroMastro == -1)
    {
        MSG_ERR("user.init(): semLibroMastro, error while getting the semaphore.");
        perror("\tsemLibroMastro ");
        shutdown(EXIT_FAILURE);
    }

    semSimulation = semget(SEM_SIM_KEY, 1, 0666);
    if(semSimulation == -1){
		MSG_ERR("user.init(): semSimulation, error while getting the semaphore.");
        perror("\tsemSimulation ");
		shutdown(EXIT_FAILURE);
	}
}

/* -------------------- LIFETIME FUNCTIONS -------------------- */

/*  */
int createTransaction()
{
    msgbuf msg;
    struct timespec timestamp;
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

    nodeReward = (int)(randomQuantity * conf[SO_REWARD] / 100);
    if(nodeReward == 0)
        nodeReward = 1;

    randomQuantity -= nodeReward;

    clock_gettime(CLOCK_REALTIME, &timestamp);
    newTr.quantity = randomQuantity;
    newTr.receiver = randomReceiverPID;
    newTr.reward = nodeReward;
    newTr.sender = getpid();
    newTr.timestamp = timestamp;

    msg.trans = newTr;
    msg.mtype = 1;

    if(msgsnd(msgTrans, &msg, sizeof(msgbuf), IPC_NOWAIT) < 0)
    {
#ifdef DEBUG
        MSG_INFO2("user.createTransaction(): Node transaction pool is full!");
        perror("\tuser.msgsnd(): ");
#endif
        return 0;
    } else {
        addToPendingList(newTr);
        /* printPendingList(); */
        tempo.tv_sec = 0;
        tempo.tv_nsec = randomNum(conf[SO_MIN_TRANS_GEN_NSEC], conf[SO_MAX_TRANS_GEN_NSEC]);
        nanosleep(&tempo, &tempo);
    }
    return 1;
}

/*  */
void getBilancio()
{
    /* calcolo bilancio - lettura with and readers solution */
    int i = 0, j = 0; 
    struct pendingTr *head = pendingList;

    bilancio = conf[SO_BUDGET_INIT];

    initReadFromShm(semBlockNumber);
    initReadFromShm(semLibroMastro);
    for (i = 0; i < *block_number; i++)
    {
        for (j = 0; j < SO_BLOCK_SIZE; j++)
        {   
            if (libroMastroArray[i].transBlock[j].receiver == getpid())
                bilancio += libroMastroArray[i].transBlock[j].quantity;

            if (libroMastroArray[i].transBlock[j].sender == getpid()){
                bilancio -= (libroMastroArray[i].transBlock[j].quantity
                            + libroMastroArray[i].transBlock[j].reward);
                removeFromPendingList(libroMastroArray[i].transBlock[j]);
            }
        }
    }
    endReadFromShm(semLibroMastro);
    endReadFromShm(semBlockNumber);

    head = pendingList;
    while(head != NULL){
        bilancio -= (head->trans.quantity + head->trans.reward);
        head = head->next;
    }

    initWriteInShm(semNodes);
    i = 0;
    while(shmUsersArray[i].pid != getpid())
        i++;
    shmUsersArray[i].budget = bilancio;
    endWriteInShm(semNodes);
}

/* Add transaction to head */
void addToPendingList(transaction tr)
{
    struct pendingTr *newEl = malloc(sizeof(struct pendingTr));
    newEl->trans = tr;
    newEl->next = pendingList;
    pendingList = newEl;
}

/* Print pending list for debugging purposes */
void printPendingList()
{
    struct pendingTr *head = pendingList;
    printf("\n\n");
    while(head != NULL){
        printf("%d:%d => ", head->trans.sender, head->trans.quantity);
        head = head->next;
    }
    printf("\n\n");
}

/* Remove transaction from pending list */
void removeFromPendingList(transaction tr)
{
    struct pendingTr *head = pendingList;
    struct pendingTr *prev = NULL;
    int found = 0;

    struct pendingTr *deleted = NULL;

    while(head != NULL && !found){
        if(head->trans.timestamp.tv_sec == tr.timestamp.tv_sec
          && head->trans.timestamp.tv_nsec == tr.timestamp.tv_nsec 
          && head->trans.sender == tr.sender
          && head->trans.receiver == tr.receiver
          && head->trans.quantity == tr.quantity
          && head->trans.reward == tr.reward){
            if(prev != NULL){
                prev->next = head->next;
            } else {
                pendingList = head->next;
            }
            deleted = head;
            found = 1;
        } else {
            prev = head;
            head = head->next;
        }
    }

    if(found)
        free(deleted);
}

/* Free pending transactions list  */
void freePendingList()
{
    if(pendingList != NULL){
        freePendingList(pendingList->next);
        free(pendingList);
    }
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
    freePendingList(pendingList);
    exit(status);
}

