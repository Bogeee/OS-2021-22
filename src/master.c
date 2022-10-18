#define _GNU_SOURCE

#include "common.h" /* set_handler */

/*
    TODO:
    -FATTO : Riorganizzazione header leggere configurazione e mettere su sharedmemory, define solo di cose necessarie, chiavi condivise su header file.h
    -Id code di messaggi una per nodo dove andranno caricate le transazioni da immettere nel libro mastro scartando quelle in eccesso, creazione transaction pool
    -Modularizzare con l'utilizzo di exec_ve utilizzando la consegna come divisione in moduli (utenti, nodi, terminazione etc..)
    -Creazione transazioni, metterle nella coda estratta a caso e continuare il ciclo
    -Ciclo dei nodi, che prenderanno le transazioni dalla coda quando ci saranno sufficenti transazioni per riempire un blocco del registro
    -Aggiungere segnale per scatenare una transazione da utente
    -Gestione della terminazione da Master: quando si verifica determinati eventi gestire killando nodi e processi e levare le ipc usate
*/
/*Funzione del master che da accesso alla scrittura/lettura dei dati in shm contenenti la configurazione file */

/* ----- PROTOTYPES ----- */

void sigterm_handler(int signum);
void shutdown();

int shmConfig;       /* ID shmem configurazione */
int shmUsers;        /* ID shmem users data */
int shmNodes;        /* ID shmem nodes data */
int shmLibroMastro;  /* ID shmem Libro Mastro */
int msgTransactions; /* ID msg queue transactions */
user *shmUsersArray; /* ID shmem Array of User PIDs */
node *shmNodesArray; /* ID shmem Array of Node PIDs */
#ifdef SO_REGISTRY_SIZE
transaction (*shmLibroMastroMatrix)[SO_REGISTRY_SIZE]; /* ID shmem Array of transactions */
#endif

/** Semaphore values
 * 0 write
 * 1 mutex
 * 2 readcount
 * **/
int semUsers;       /* Semaphore for shmem access on the Array of User PIDs */
int semNodes;       /* Semaphore for shmem access on the Array of Node PIDs */
int semLibroMastro; /* Semaphore for shmem access on the Libro Mastro */
int semSimulazione; /* Semaphore for the simulation */

int main(int argc, char **argv)
{
    unsigned long *conf;   /* Array of conf. values attached to shmem */
    pid_t child_pid, wpid; /* child_pid is used for the fork, wpid unused */

    /*** Certe robe vanno poi spostate in user.c e node.c, ora le metto per essere
     * standard C89 compliant ***/
    int status = 0;     /*  */
    int fails = 0;      /* used with SO_RETRY */
    int bilancio;       /*  */
    transaction newTr;  /* new transaction */
    int randomReceiver; /* Random user */
    int randomNode;     /* Random node */
    int randomQuantity; /* Random quantity for the transaction */
    int nodeReward;     /* Transaction reward */
    int i = 0, j = 0, k = 0;

    /*system("clear"); */
    /* Creating the shared memory segment */
    shmConfig = shmget(SHM_ENV_KEY, sizeof(unsigned long) * N_RUNTIME_CONF_VALUES,
                       IPC_CREAT | IPC_EXCL | 0666);
    if (shmConfig == -1)
    {
        MSG_ERR("main: shmConfig, error while creating the shared memory segment.");
        perror("\tshmConfig");
        shutdown();
        exit(EXIT_FAILURE);
    }

    conf = shmat(shmConfig, NULL, 0);

    /* Gets conf from Env variables, Writes configuration to shared memory */
    if (get_configuration(conf) == 0)
    {
        MSG_ERR("main: get_configuration, error while loading source file.");
        shutdown();
        exit(EXIT_FAILURE);
    };

    semUsers = semget(SEM_USER_KEY, 3, IPC_CREAT | 0666);
    if (semUsers == -1)
    {
        MSG_ERR("main: semUsers, error while creating the semaphore.");
        perror("\tsemUsers");
        shutdown();
        exit(EXIT_FAILURE);
    }

    semNodes = semget(SEM_NODE_KEY, 3, IPC_CREAT | 0666);
    if (semNodes == -1)
    {
        MSG_ERR("main: semNodes, error while creating the semaphore.");
        perror("\tsemNodes");
        shutdown();
        exit(EXIT_FAILURE);
    }

    semLibroMastro = semget(SEM_LIBROMASTRO_KEY, 3, IPC_CREAT | 0666);
    if (semLibroMastro == -1)
    {
        MSG_ERR("main: semLibroMastro, error while creating the semaphore.");
        perror("\tsemLibroMastro");
        shutdown();
        exit(EXIT_FAILURE);
    }

    semSimulazione = semget(SEM_SIM_KEY, 1, IPC_CREAT | 0666);
    if (semSimulazione == -1)
    {
        MSG_ERR("main: semSimulazione, error while creating the semaphore.");
        perror("\tsemSimulazione");
        shutdown();
        exit(EXIT_FAILURE);
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

    /* Creating shmem segment for Users */
    shmUsers = shmget(SHM_USER_KEY, sizeof(user) * conf[SO_USERS_NUM], IPC_CREAT | IPC_EXCL | 0666);
    if (shmUsers == -1)
    {
        MSG_ERR("main: shmUsers, error while creating the shared memory segment.");
        perror("\tshmUsers");
        shutdown();
        exit(EXIT_FAILURE);
    }

    /* Creating shmem segment for Nodes */
    shmNodes = shmget(SHM_NODE_KEY, sizeof(node) * conf[SO_NODES_NUM], IPC_CREAT | IPC_EXCL | 0666);
    if (shmNodes == -1)
    {
        MSG_ERR("main: shmNodes, error while creating the shared memory segment.");
        perror("\tsemNodes");
        shutdown();
        exit(EXIT_FAILURE);
    }

    /* Creating shmem segment for the Libro Mastro */
    shmLibroMastro = shmget(SHM_LIBROMASTRO_KEY, sizeof(transaction[SO_REGISTRY_SIZE][SO_BLOCK_SIZE]),
                            IPC_CREAT | IPC_EXCL | 0666);
    if (shmLibroMastro == -1)
    {
        MSG_ERR("main: shmLibroMastro, error while creating the shared memory segment.");
        perror("\tsemLibroMastro");
        shutdown();
        exit(EXIT_FAILURE);
    }

    msgTransactions = msgget(MSG_TRANS_KEY, IPC_CREAT | IPC_EXCL | 0666);
    if (msgTransactions == -1)
    {
        MSG_ERR("main: msgTransactions, error while creating the message queue.");
        perror("\tmsgTransactions");
        shutdown();
        exit(EXIT_FAILURE);
    }

    /*atexit(shutdown);*/

    /* Init seed for the random number generation */
    srand(time(NULL));

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
            if (shmat(shmUsers, NULL, 0) == (void *)-1)
                perror("shmUsersArray shmat ");
            if (shmUsersArray[i].pid == 0)
            {
                shmUsersArray[i].pid = getpid();
                shmUsersArray[i].budget = conf[SO_BUDGET_INIT];
            }
            printf("Child put %d in shmUsers[%d]\n", getpid(), i);
            shmdt((void *)shmUsersArray);
            if (shmUsersArray == (void *)-1)
                perror("shmUsersArray shmdt ");
            endWriteInShm(semUsers);

            /* Shmem read of user pids and budgets */
            initReadFromShm(semUsers);
            shmUsersArray = shmat(shmUsers, NULL, 0);
            if (shmUsersArray == (void *)-1)
                perror("shmUsersArray shmat ");
            for (k = 0; k < conf[SO_USERS_NUM]; k++)
            {
                printf("shmUsers[%d] value : %d, con budget %d\n", k, shmUsersArray[k].pid,
                       shmUsersArray[k].budget);
            }
            shmdt((void *)shmUsersArray);
            if (shmUsersArray == (void *)-1)
                perror("shmUsersArray shmdt ");
            endReadFromShm(semUsers);

            /* Waiting that the nodes and the users are ready and active */
            reserveSem(semSimulazione, 0);
            while (semctl(semSimulazione, 0, GETVAL, 0) != 0)
                sleep(1);

            /* printf("user - non manca più nessuno !\n"); */

            execve("./bin/user", argv, NULL);

            exit(0);
            break;
        default:
            /* father branch */
            /* wait(NULL); */
            break;
        }
    }

    /* Nodes creating loop */
    for (i = 0; i < conf[SO_NODES_NUM]; i++)
    {
        child_pid = fork();
        switch (child_pid)
        {
        case -1:
            perror("Fork node error ");
            exit(0);
            break;
        case 0:
            /* child branch */

            /* Shmem write */
            initWriteInShm(semNodes);
            shmNodesArray = (node *)shmat(shmNodes, NULL, 0);
            if (shmNodesArray == (void *)-1)
                perror("shmNodesArray shmat ");
            if (shmNodesArray[i].pid == 0)
            {
                shmNodesArray[i].pid = getpid();
                shmNodesArray[i].reward = 0;
            }
            printf("Child put %d in shmNodes[%d]\n", getpid(), i);
            shmdt((void *)shmNodesArray);
            if (shmNodesArray == (void *)-1)
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

            /* printf("nodi - non manca più nessuno !\n"); */
            exit(0);
            break;
        default:
            /* father branch */
            break;
        }
    }

    /* Attesa del semaforo per far partire la simulazione */

    /* Attesa che tutti i figli siano siano conclusi */
    while (wait(NULL) != -1)
        ;

    /*printf("lm! write %d\n", semctl(semLibroMastro, 0, GETVAL, 0));
    printf("lm! mutex %d\n", semctl(semLibroMastro, 1, GETVAL, 0));
    printf("lm! readers %d\n", semctl(semLibroMastro, 2, GETVAL, 0));

    printf("user! write %d\n", semctl(semUsers, 0, GETVAL, 0));
    printf("user! mutex %d\n", semctl(semUsers, 1, GETVAL, 0));
    printf("user! readers %d\n", semctl(semUsers, 2, GETVAL, 0));

    printf("nodes! write %d\n", semctl(semNodes, 0, GETVAL, 0));
    printf("nodes! mutex %d\n", semctl(semNodes, 1, GETVAL, 0));
    printf("nodes! readers %d\n", semctl(semNodes, 2, GETVAL, 0));*/

    shmdt((void *)conf);
    if (conf == (void *)-1)
    {
        MSG_ERR("main: conf, error while detaching the configuration shmem segment.");
        shutdown();
        exit(EXIT_FAILURE);
    }

    /* gestione CTRL+C e altri segnali */
    set_handler(SIGTERM, &sigterm_handler);
    set_handler(SIGINT, &sigterm_handler);

    shutdown();
    exit(EXIT_SUCCESS);
    return 0;
}

/* non funge */
void sigterm_handler(int signum)
{
    fprintf(stdout, "[%sINFO%s] Ricevuto il segnale %s, arresto la simulazione\n",
            COLOR_YELLOW, COLOR_FLUSH, strsignal(signum));
    shutdown();
    exit(EXIT_SUCCESS);
}

/* procedura di terminazione, clear IPC, ... */
void shutdown()
{
    printf("\nEhi sono il master guarda che sto uscendo...");
    /* Rimozione IPC */
    shmctl(shmUsers, IPC_RMID, NULL);
    shmctl(shmNodes, IPC_RMID, NULL);
    shmctl(shmLibroMastro, IPC_RMID, NULL);
    shmctl(shmConfig, IPC_RMID, NULL);

    semctl(semUsers, 0, IPC_RMID, 0);
    semctl(semNodes, 0, IPC_RMID, 0);
    semctl(semLibroMastro, 0, IPC_RMID, 0);
    semctl(semSimulazione, 0, IPC_RMID, 0);

    msgctl(msgTransactions, IPC_RMID, NULL);
}