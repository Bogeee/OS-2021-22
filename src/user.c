#define _GNU_SOURCE
#include "common.h"


unsigned long *conf;
int shmConfig; 
int shmUsers; 
int shmLibroMastro;
int semLibroMastro; 
int semUsers;
user *shmUsersArray;
#ifdef SO_REGISTRY_SIZE
transaction (*shmLibroMastroMatrix)[SO_REGISTRY_SIZE]; /* ID shmem Array of transactions */
#endif

void shutdown(int);
int getBilancio();
void createTransaction();
void signalHandler();

int main(int argc, char ** argv){

    int fails = 0; /* used with SO_RETRY */
    int bilancio; /*  */
    int status = 0; /*  */

    /* Creating the shared memory segment */
    shmConfig = shmget(SHM_ENV_KEY, 0, 
							0666);
    if (shmConfig == -1){
		MSG_ERR("user: shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
		shutdown(EXIT_FAILURE);
	}
	
    conf = shmat(shmConfig, NULL, SHM_RDONLY);

    semUsers = semget(SEM_USER_KEY, 3, 0666);
    if(semUsers == -1){
		MSG_ERR("user: semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
		shutdown(EXIT_FAILURE);
	}

	semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, 0666);
    if(semLibroMastro == -1){
		MSG_ERR("user: semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}

    shmUsers = shmget(SHM_USER_KEY, sizeof(user) * conf[SO_USERS_NUM], 0666);
    if (shmUsers == -1){
		MSG_ERR("main: shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
		shutdown(EXIT_FAILURE);
	}

    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(transaction[SO_REGISTRY_SIZE][SO_BLOCK_SIZE]),
							0666);
    if (shmLibroMastro == -1){
		MSG_ERR("main: shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro");
		shutdown(EXIT_FAILURE);
	}

    if (signal(SIGUSR1, signalHandler) == SIG_ERR)  {
		MSG_ERR("main: signalHandler, error.");
        perror("signal SIGUSR1 error");
        shutdown(EXIT_FAILURE);
    }

    /* Starting user loop */
    while(fails < conf[SO_RETRY]) {
        bilancio = getBilancio();
        if(bilancio >= 2) 
        {
            createTransaction(bilancio);
        } else {
            fails++;
        }
    }
    return 0;
}

void shutdown(int status) 
{
	/* Rimozione IPC */
	shmctl(shmUsers, IPC_RMID, NULL);
	/*shmctl(shmNodes, IPC_RMID, NULL);*/
	shmctl(shmLibroMastro, IPC_RMID, NULL);
	shmctl(shmConfig, IPC_RMID, NULL);

	semctl(semUsers, 0, IPC_RMID, 0);
	/*semctl(semNodes, 0, IPC_RMID, 0);*/
	semctl(semLibroMastro, 0, IPC_RMID, 0);
	/*semctl(semSimulazione, 0, IPC_RMID, 0);*/

	/*msgctl(msgTransactions, IPC_RMID, NULL);*/
	exit(status);
}

void createTransaction(int bilancio) {

    transaction newTr; /* new transaction */
    int randomReceiver; /* Random user */
    int randomNode; /* Random node */
    int randomQuantity; /* Random quantity for the transaction */
    int nodeReward; /* Transaction reward */

    /*shutdown(EXIT_SUCCESS);*/
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
    
    /* TODO : Invia al nodo = carica nel msgQueue del nodo */ 
}

int getBilancio() {
    /* TODO: controllo transazioni fallite */
    /* calcolo bilancio - lettura with and readers solution */
    int bilancio = conf[SO_BUDGET_INIT];
    int k = 0, j = 0;
    initReadFromShm(semLibroMastro);
    shmLibroMastroMatrix = shmat(shmLibroMastro, NULL, 0);
    if(shmLibroMastroMatrix == (void *) -1)
        perror("shmLibroMastroMatrix shmat ");
    for (k = 0; k < SO_REGISTRY_SIZE; k++)
    {
        for (j = 0; j < SO_BLOCK_SIZE; j++)
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
    
    /*printf("[%d] ha bilancio uguale a : %d\n", getpid(), bilancio);*/

    return bilancio;
}

void signalHandler() {
    int bilancio = getBilancio();
    printf("Segnale ricevuto - creazione transazione\n");
    if(bilancio > 0)
        createTransaction(bilancio);
}