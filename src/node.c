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

/* Signal Handlers */
void sigint_handler();

/* Termination */
void shutdown(int status);

/* -------------------- GLOBAL VARIABLES -------------------- */

/**** SHARED MEMORY IDs ****/
int shmConfig;        /* ID shmem configuration */
int shmNodes;         /* ID shmem nodes data */
int shmLibroMastro;   /* ID shmem Libro Mastro */
int shmBlockNumber;   /* ID shmem libro mastro block number */

/**** SHARED MEMORY ATTACHED VARIABLES ****/
node *shmNodesArray;          /* Shmem Array of Node PIDs */
block *libroMastroArray;      /* Shmem Array of blocks */
unsigned int *block_number;   /* Shmem number of the last block */
unsigned long *conf;          /* Shmem Array of configuration values */

/**** MESSAGE QUEUE ID ****/
int myTransactionsMsg;  /* ID for the message queue */

/**** SEMAPHORE IDs ****/
int semNodes;        /* Semaphore for shmem access on the Array of Node PIDs */
int semLibroMastro;  /* Semaphore for shmem access on the Libro Mastro */
int semBlockNumber;  /* Semaphore for the last block number */
int semSimulation;   /* Semaphore for the simulation */

/*** Global variables ***/
int reward_budget;	/* Node's reward */
int unproc_trans;	/* Number of unprocessed transactions before term. */
int my_index;		/* Node's index in the shmNodesArray */
int count;    		/* Transaction number in a block */
pid_t my_pid;

int main()
{
	msgbuf msg;
	transaction reward;
	struct timespec timestamp;
	struct timespec t;
	block transSet;

	ssize_t num_bytes;
	int count = 0;
	int i = 0;
	int sum_rewards = 0;

	init();
#ifdef DEBUG
	printf("[INFO] node.main(%d): MSG_QUEUE Waiting for messages...\n", my_pid);
#endif
	while(1){
		num_bytes = 0;
		num_bytes = msgrcv(myTransactionsMsg, &msg, sizeof(msg), 0, 0);

		if (num_bytes > 0) {
			/* received a good message */
			/* adding the transaction to a local block */
			transSet.transBlock[count] = msg.trans;
			count++;

			if(count == SO_BLOCK_SIZE-1){
				/* adding the reward transaction */
				sum_rewards = 0;
				for(i = 0; i <= count; i++){
					sum_rewards += transSet.transBlock[i].reward;
				}
				reward_budget += sum_rewards;
				clock_gettime(CLOCK_REALTIME, &timestamp);

				reward.timestamp = timestamp;
				reward.sender = TRANS_REWARD_SENDER;
				reward.receiver = my_pid;
				reward.quantity = sum_rewards;
				reward.reward = 0;
				
				transSet.transBlock[count] = reward;

				block_signals(2, SIGINT, SIGTERM);
				/* Use the current block_number value and increment it */
				initWriteInShm(semBlockNumber);
				transSet.block_number = *block_number;
				/* Not releasing the semaphore yet, I use the block_number
					as index for the libro mastro's array of blocks */

				/* processing */
				t.tv_sec = 0;
				t.tv_nsec = randomNum(conf[SO_MIN_TRANS_PROC_NSEC], 
									  conf[SO_MAX_TRANS_GEN_NSEC]);
				nanosleep(&t, &t);

				/* libro mastro is full */
				if(*block_number == SO_REGISTRY_SIZE){
					/* send signal to master process */
					unblock_signals(2, SIGINT, SIGTERM);
					endWriteInShm(semBlockNumber);
					kill(getppid(), SIGUSR1);
					pause();
				} else {
					initWriteInShm(semLibroMastro);
					libroMastroArray[*block_number] = transSet;
					endWriteInShm(semLibroMastro);

					*block_number = *block_number + 1;
				}
				endWriteInShm(semBlockNumber);

				initWriteInShm(semNodes);
				shmNodesArray[my_index].reward = reward_budget;
				endWriteInShm(semNodes);

				/* we can start writing another block */
				count = 0;

				unblock_signals(2, SIGINT, SIGTERM);
			}
		}

		/* Error handling */
		if (errno == EINTR) {
			fprintf(stderr, 
					"node.main(%d): interrupted by a signal while waiting for a message "
					"on Q_ID=%d. Trying again...\n",
					my_pid, myTransactionsMsg);
			continue;
		}
		if (errno == EIDRM) {
			fprintf(stderr, "node.main(%d): The Q_ID=%d was removed. Ending!\n", 
					my_pid,
					myTransactionsMsg);
			shutdown(EXIT_FAILURE);
		}
	}
	
	return 0;
}

/* -------------------- INITIALIZATION FUNCTIONS -------------------- */

/* Accessing all the IPC objects, setting the SIGINT Handler */
void init()
{
	int i = 0;
	struct sembuf s;
    s.sem_num = 0;
    s.sem_op = 0;
    s.sem_flg = 0;

	my_pid = getpid();
	reward_budget = 0;
	unproc_trans = 0;

	init_conf();
	init_sharedmem();
	init_semaphores();
	init_msgqueue();

	/* Initializes seed for the random number generation */ 
    srand(time(NULL));

	/* Master wants to kill the node */
	set_handler(SIGINT, sigint_handler);

	block_signals(2, SIGINT, SIGTERM);
	initReadFromShm(semNodes);
	i = 0;
	while(shmNodesArray[i].pid != my_pid)
		i++;
	my_index = i;
	endReadFromShm(semNodes);
	unblock_signals(2, SIGINT, SIGTERM);

	/* Waiting that the other nodes are ready and active */
	reserveSem(semSimulation, 0);
    if(semop(semSimulation, &s, 1) == -1){
#ifdef DEBUG
        MSG_ERR("node.init(): error while waiting for zero on semSimulation.");
        perror("\tsemSimulation: ");
#endif
	}
}

/* Accessing the configuration shared memory segment in READ ONLY */
void init_conf()
{
	shmConfig = shmget( SHM_ENV_KEY, 
						sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
						SHM_RDONLY);
	if (shmConfig == -1){
		MSG_ERR("node.init(): shmConfig, error while getting the shared memory segment.");
        perror("\tshmConfig ");
		shutdown(EXIT_FAILURE);
	}
    conf = shmat(shmConfig, NULL, 0);
}

/* Accessing the other shared memory segments and attaching */
void init_sharedmem()
{
    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0600);
	if (shmNodes == -1){
		MSG_ERR("node.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tshmNodes ");
		shutdown(EXIT_FAILURE);
	}
	shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

	/* Accessing shmem segment for the libro mastro's block number */
    shmBlockNumber = shmget(SHM_BLOCK_NUMBER, sizeof(unsigned int), 0600);
    if (shmBlockNumber == -1){
		MSG_ERR("node.init(): shmBlockNumber, error while creating the shared memory segment.");
        perror("\tshmBlockNumber ");
		shutdown(EXIT_FAILURE); 
	}
    block_number = (unsigned int *)shmat(shmBlockNumber, NULL, 0);

	/* Accessing shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, 
							sizeof(block) * SO_REGISTRY_SIZE, 
							0600);
    if (shmLibroMastro == -1){
		MSG_ERR("node.init(): shmLibroMastro, error while creating the shared memory segment.");
        perror("\tshmLibroMastro ");
		shutdown(EXIT_FAILURE);
	}
	libroMastroArray = (block *)shmat(shmLibroMastro, NULL, 0);
}

/* Accessing to the semaphores for the shared memory */
void init_semaphores()
{
	semNodes = semget(SEM_NODE_KEY, 3, 0600);
	if(semNodes == -1){
		MSG_ERR("node.init(): semNodes, error while getting the semaphore.");
        perror("\tsemNodes ");
		shutdown(EXIT_FAILURE);
	}

	semBlockNumber = semget(SEM_BLOCK_NUMBER, 3, 0600);
	if(semBlockNumber == -1){
		MSG_ERR("node.init(): semBlockNumber, error while getting the semaphore.");
        perror("\tsemBlockNumber ");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, 0600);
    if(semLibroMastro == -1){
		MSG_ERR("node.init(): semLibroMastro, error while getting the semaphore.");
        perror("\tsemLibroMastro ");
		shutdown(EXIT_FAILURE);
	}

	semSimulation = semget(SEM_SIM_KEY, 1, 0600);
    if(semSimulation == -1){
		MSG_ERR("node.init(): semSimulation, error while getting the semaphore.");
        perror("\tsemSimulation ");
		shutdown(EXIT_FAILURE);
	}
}

/* Accessing the message queue, SO_TP_SIZE management */
void init_msgqueue()
{
	struct msqid_ds msg_params;
	msglen_t msg_max_size_no_root;

	myTransactionsMsg = msgget(ftok(FTOK_PATHNAME_NODE, my_pid), 0600);
    if(myTransactionsMsg == -1){
		MSG_ERR("node.init(): myTransitionsMsg, error while getting the message queue.");
        perror("\tmyTransitionsMsg ");
		shutdown(EXIT_FAILURE);
	}

	/* SO_TP_SIZE management */
	msgctl(myTransactionsMsg, IPC_STAT, &msg_params);

	/* 
	 * Setting the max msgqueue size, when TP is full, 
	 * the msgsnd() fails with EAGAIN
	 */
	msg_params.msg_qbytes = sizeof(msgbuf) * conf[SO_TP_SIZE];
	msgctl(myTransactionsMsg, IPC_SET, &msg_params);
}

/* -------------------- SIGNAL HANDLERS -------------------- */

/* Receiving SIGINT from master process */
void sigint_handler()
{
	msgbuf msg;
	while(msgrcv(myTransactionsMsg, &msg, sizeof(msg), 0, IPC_NOWAIT) != -1)
		unproc_trans++;
	
	block_signals(2, SIGINT, SIGTERM);
	initWriteInShm(semNodes);
	shmNodesArray[my_index].unproc_trans = unproc_trans + count;
	endWriteInShm(semNodes);
	
	shutdown(EXIT_SUCCESS);
}

/* -------------------- TERMINATION FUNCTIONS -------------------- */

void shutdown(int status)
{
    /* Rimozione IPC */
    /* detach the shmem for the nodes Array */
    if(shmdt((void *)shmNodesArray) == -1){
        MSG_ERR("node.shutdown(): shmNodesArray, error while detaching "
                "the shmNodesArray shmem segment.");
	}

	/* detach the shmem for the libro mastro */
    if(shmdt((void *)libroMastroArray) == -1){
        MSG_ERR("node.shutdown(): libroMastroArray, error while detaching "
                "the libroMastroArray shmem segment.");
	}

    /* detach the shmem for the last block number */
    if(shmdt((void *)block_number) == -1){
        MSG_ERR("node.shutdown(): block_number, error while detaching "
                "the block_number shmem segment.");
	}

	/* detach the shmem for the configuration */
    if(shmdt((void *)conf) == -1){
        MSG_ERR("node.shutdown(): conf, error while detaching "
                "the conf shmem segment.");
	}

    shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmBlockNumber, IPC_RMID, NULL);
    shmctl(shmConfig, IPC_RMID, NULL);

	msgctl(myTransactionsMsg, IPC_RMID, NULL);

	exit(status);
}
