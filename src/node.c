#define _GNU_SOURCE
#include <errno.h>
#include <time.h>
#include <string.h> /* bzero */
#include <signal.h> /* sigaction, SIG* */
#include "common.h"
#include "bashprint.h"

/* ----- prototypes ----- */
void init();

/* ----- global variables ----- */
int shmConfig; /* ID shmem configurazione */
unsigned long *conf; /* conf array, shmem read only */
int shmNodes;  /* ID shmem nodes data */
int semNodes;  /* Semaphore for shmem access on the Array of Node PIDs */
node *shmNodesArray; /* ID shmem Array of Node PIDs */

int myTransitionsMsg; /* ID for the message queue */

/*  */
struct sigaction sa;


/* TODO:
   SCRITTURA LIBRO MASTRO,
   SO_TP_SIZE?
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
	printf("MSG_QUEUE: Waiting for messages...\n");
	while(1){
		num_bytes = msgrcv(myTransitionsMsg, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

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
				/* Read from shared memory the number of the next block */
				/* transSet.blockNumber = ?; */

				/* processing */
				t.tv_sec = 0;
				t.tv_nsec = randomNum(SO_MIN_TRANS_PROC_NSEC, SO_MAX_TRANS_GEN_NSEC);
				nanosleep(&t, &t);

				/* writing to LibroMastro */
				/* scrittura */

				/*  */
				count = 0;
			}
		}

		/* now error handling */
		if (errno == EINTR) {
			fprintf(stderr, "(PID=%d): interrupted by a signal while waiting for a message of type %ld on Q_ID=%d. Trying again\n",
				getpid(), getpid(), myTransitionsMsg);
			continue;
		}
		if (errno == EIDRM) {
			printf("The Q_ID=%d was removed. Let's terminate\n", myTransitionsMsg);
			exit(0);
		}
		if (errno == ENOMSG) {
			printf("No message of type=%ld in Q_ID=%d. Not going to wait\n", getpid(), myTransitionsMsg);
			exit(0);
		}
	}
	
	return 0;
}

/*  */
void init()
{
	shmConfig = shmget(SHM_ENV_KEY, sizeof(unsigned long) * N_RUNTIME_CONF_VALUES, 
						 SHM_RDONLY);
	if (shmConfig == -1){
		MSG_ERR("node.init(): shmConfig, error while getting the shared memory segment.");
        perror("\tshmConfig");
		shutdown(EXIT_FAILURE);
	}
    conf = shmat(shmConfig, NULL, 0);

	semNodes = semget(SEM_NODE_KEY, 3, 0600);
	if(semNodes == -1){
		MSG_ERR("node.init(): semNodes, error while getting the semaphore.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}

    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], 0600);
	if (shmNodes == -1){
		MSG_ERR("node.init(): shmNodes, error while creating the shared memory segment.");
        perror("\tsemNodes");
		shutdown(EXIT_FAILURE);
	}
	shmNodesArray = (node *)shmat(shmNodes, NULL, 0);

	myTransitionsMsg = msgget(ftok(FTOK_PATHNAME_NODE, getpid()), 0600);
    if(myTransitionsMsg == -1){
		MSG_ERR("node.init(): myTransitionsMsg, error while getting the message queue.");
        perror("\tmyTransitionsMsg");
		shutdown(EXIT_FAILURE);
	}

	/* Master wants to kill the node */
	bzero(&sa, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
}

void sigint_handler(){

}