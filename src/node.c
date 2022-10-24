#define _GNU_SOURCE
#include <errno.h>
#include <time.h>
#include <signal.h> /* SIG* */
#include "common.h"
#include "bashprint.h"

/* -------------------- PROTOTYPES -------------------- */

/* Initialization */
void init();
void init_conf();
void init_sharedmem();
void init_semaphores();
void init_msgqueue();

/* Termination */
void shutdown();

/* -------------------- GLOBAL VARIABLES -------------------- */

/**** SHARED MEMORY IDs ****/
int shmConfig;        /* ID shmem configuration */
int shmNodes;         /* ID shmem nodes data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int shmBlockNumber;   /* ID shmem libro mastro block number */

/**** SHARED MEMORY ATTACHED VARIABLES ****/
node *shmNodesArray;          /* Shmem Array of Node PIDs */
block (*libroMastroArray)[SO_REGISTRY_SIZE];    /* Shmem Array of blocks */
unsigned int block_number;    /* Shmem number of the last block */
unsigned long *conf;          /* Shmem Array of configuration values */

/**** MESSAGE QUEUE ID ****/
int myTransactionsMsg;  /* ID for the message queue */

/**** SEMAPHORE IDs ****/
int semNodes;        /* Semaphore for shmem access on the Array of Node PIDs */
int semLibroMastro;  /* Semaphore for shmem access on the Libro Mastro */
int semBlockNumber;  /* Semaphore for the last block number */

/* TODO:
	maschera segnali
   TEST CODICE */
int main()
{
	msgbuf msg;
	transaction transBlock[conf[SO_BLOCK_SIZE]];
	transaction reward;
	struct timespec t;
	block transSet;

	int num_bytes;
	int count = 0;
	int i = 0;
	int sum_rewards;

	init();
	printf("node.main(): MSG_QUEUE Waiting for messages...\n");
	while(1){
		num_bytes = msgrcv(myTransactionsMsg, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

		if (num_bytes > 0) {
			/* received a good message */
			/* adding the transaction to a local block */
			transBlock[count] = msg.trans;
			count++;

			if(count == conf[SO_BLOCK_SIZE]-2){
				/* adding the reward transaction */
				for(i = 0; i <= count; i++){
					sum_rewards += transBlock[i].reward;
				}
				reward.timestamp = clock_gettime(CLOCK_REALTIME, &t);
				reward.sender = TRANS_REWARD_SENDER;
				reward.receiver = getpid();
				reward.quantity = sum_rewards;
				reward.reward = 0;
				
				transBlock[++count] = reward;

				/* creating block */
				transSet.transBlock = transBlock;

				/* Use the current block_number value and increment it */
				initWriteInShm(shmBlockNumber);
				transSet.block_number = block_number;
				/* Not releasing the semaphore yet, I use the block_number
					as index for the libro mastro's array of blocks */

				/* processing */
				t.tv_sec = 0;
				t.tv_nsec = randomNum(conf[SO_MIN_TRANS_PROC_NSEC], conf[SO_MAX_TRANS_GEN_NSEC]);
				nanosleep(&t, &t);

				initWriteInShm(shmLibroMastro);
				*libroMastroArray[block_number] = transSet;
				endWriteInShm(shmLibroMastro);

				block_number++;
				/* libro mastro is full */
				if(block_number == conf[SO_REGISTRY_SIZE]){
					/* send signal to master process */
					kill(getppid(), SIGUSR1);
				}
				endWriteInShm(shmBlockNumber);
				
				/* we can start writing another block */
				count = 0;
			}
		}

		/* now error handling */
		if (errno == EINTR) {
			fprintf(stderr, "(PID=%d): interrupted by a signal while waiting for a message of type %ld on Q_ID=%d. Trying again\n",
				getpid(), getpid(), myTransactionsMsg);
			continue;
		}
		if (errno == EIDRM) {
			printf("The Q_ID=%d was removed. Let's terminate\n", myTransactionsMsg);
			exit(0);
		}
		if (errno == ENOMSG) {
			printf("No message of type=%ld in Q_ID=%d. Not going to wait\n", getpid(), myTransactionsMsg);
			exit(0);
		}
	}
	
	return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

/* Accessing all the IPC objects, setting the SIGINT Handler */
void init()
{
	init_conf();
	init_sharedmem();
	init_semaphores();
	init_msgqueue();

	/* Master wants to kill the node */
	set_handler(SIGINT, sigint_handler);
}

/* Accessing the configuration shared memory segment in READ ONLY */
void init_conf()
{
	shmConfig = shmget( SHM_ENV_KEY, 
						sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
						SHM_RDONLY);
	if (shmConfig == -1){
		MSG_ERR("node.init(): shmConfig, error while getting the shared memory segment.");
        perror("\tshmConfig");
		shutdown(EXIT_FAILURE);
	}
    conf = shmat(shmConfig, NULL, 0);
}

/* Accessing the other shared memory segments and attaching */
void init_sharedmem()
{
    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0666);
	if (shmNodes == -1){
		MSG_ERR("node.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes");
		shutdown(EXIT_FAILURE);
	}
	shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

	/* Accessing shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, sizeof(unsigned int), 0666);
    if (shmBlockNumber == -1){
		MSG_ERR("node.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber");
		shutdown(EXIT_FAILURE); 
	}
    block_number = shmat(shmBlockNumber, NULL, 0);

	/* Accessing shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(block[conf[SO_REGISTRY_SIZE]]), 0666);
    if (shmLibroMastro == -1){
		MSG_ERR("node.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tshmLibroMastro");
		shutdown(EXIT_FAILURE);
	}
	libroMastroArray = shmat(shmLibroMastro, NULL, 0);
}

/* Accessing to the semaphores for the shared memory */
void init_semaphores()
{
	semNodes = semget(SEM_NODE_KEY, 3, 0666);
	if(semNodes == -1){
		MSG_ERR("node.init(): semNodes, error while getting the semaphore.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

	semBlockNumber = semget(SEM_BLOCK_NUMBER, 1, 0666);
	if(semBlockNumber == -1){
		MSG_ERR("node.init(): semBlockNumber, error while getting the semaphore.");
        perror("\tsemBlockNumber");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, IPC_CREAT | 0666);
    if(semLibroMastro == -1){
		MSG_ERR("node.init(): semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}
}

/* Accessing the message queue, SO_TP_SIZE management */
void init_msgqueue()
{
	struct msqid_ds *msg_params;
	msglen_t msg_max_size_no_root;

	myTransactionsMsg = msgget(ftok(FTOK_PATHNAME_NODE, getpid()), 0666);
    if(myTransactionsMsg == -1){
		MSG_ERR("node.init(): myTransitionsMsg, error while getting the message queue.");
        perror("\tmyTransitionsMsg");
		shutdown(EXIT_FAILURE);
	}

	/* SO_TP_SIZE management */
	msgctl(myTransactionsMsg, IPC_STAT, msg_params);
	msg_max_size_no_root = msg_params->msg_qbytes;

	if(sizeof(msgbuf) * conf[SO_TP_SIZE] > msg_max_size_no_root){
		MSG_ERR("node.init(): msg_queue_size, the transaction pool is bigger than the maximum msgqueue size.");
		MSG_INFO2("node.init(): you should change the MSGMNB kernel info with root privileges.");
		shutdown(EXIT_FAILURE);
	}

	/* Setting the max msgqueue size, when TP is full, the msgsnd() fails with EAGAIN */
	msg_params->msg_qbytes = sizeof(msgbuf) * conf[SO_TP_SIZE];
	msgctl(myTransactionsMsg, IPC_SET, msg_params);
}


/* -------------------- SIGNAL HANDLERS -------------------- */

/* Receiving SIGINT from master process */
void sigint_handler()
{
	shutdown(EXIT_SUCCESS);
}

/* -------------------- TERMINATION FUNCTIONS -------------------- */

void shutdown(int status)
{
    /* Rimozione IPC */
    /* detach the shmem for the last block number */
    shmdt((void *)shmNodesArray);
    if(shmNodesArray == (void *) -1){
        MSG_ERR("node.shutdown(): shmNodesArray, error while detaching "
                "the shmNodesArray shmem segment.");
	}

	/* detach the shmem for the libro mastro */
    shmdt((void *)libroMastroArray);
    if(libroMastroArray == (void *) -1){
        MSG_ERR("node.shutdown(): libroMastroArray, error while detaching "
                "the libroMastroArray shmem segment.");
	}

    /* detach the shmem for the last block number */
    shmdt((void *)block_number);
    if(block_number == (void *) -1){
        MSG_ERR("node.shutdown(): block_number, error while detaching "
                "the block_number shmem segment.");
	}

	/* detach the shmem for the configuration */
    shmdt((void *)conf);
    if(conf == (void *) -1){
        MSG_ERR("node.shutdown(): conf, error while detaching "
                "the conf shmem segment.");
	}

    shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmBlockNumber, IPC_RMID, NULL);
    shmctl(shmConfig, IPC_RMID, NULL);

    semctl(semNodes, 0, IPC_RMID, 0);
    semctl(semLibroMastro, 0, IPC_RMID, 0);
	semctl(semBlockNumber, 0, IPC_RMID, 0);

	msgctl(myTransactionsMsg, IPC_RMID, NULL);

	exit(status);
}
