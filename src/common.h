#ifndef __COMMON_H
#define __COMMON_H

/*
#define SEM_KEY 117
#define START_SEM 0
#define MUTEX_SEM 1
#define ROOM_EMPTY_SEM 2
#define TURNSTILE_SEM 3

#define SAFE_KILL_SEM_KEY 34287
#define SHM_KEY 43972
#define MSG_KEY 23477
#define MNG_MSG_KEY 32648

#define TYPE_A_PATH "./tipo_a"
#define TYPE_B_PATH "./tipo_b"

typedef struct {
	pid_t pid;
	char tipo;
	unsigned long genoma;
} individuo;

typedef struct {
	long mtype;
	char response;
} responseFromA;

typedef struct {
	long mtype;
	pid_t pid;
	unsigned long genome;
	unsigned long uid;
} requestFromB;

typedef struct {
	long mtype;
	pid_t partner_pid;
} messageToManager;

#define RESPONSE_SIZE (sizeof(char))
#define REQUEST_SIZE (sizeof(pid_t) + sizeof(unsigned long) * 2)
#define MESSAGE_TO_MANAGER_SIZE (sizeof(pid_t))
*/

/* Riduce di 1 il valore del semaforo (acquisisce risorsa) */
int sem_wait(int s_id, unsigned short sem_num) ;

/* Aumenta di 1 il valore del semaforo (rilascia risorsa) */
int sem_signal(int s_id, unsigned short sem_num);

/* Esegue una semop */
int sem_cmd(int s_id, unsigned short sem_num, short sem_op, short sem_flg);

/* Blocca i segnali elencati tra gli argomenti */
/* Restituisce la vecchia maschera */
sigset_t block_signals(int count, ...);

/* Sblocca i segnali elencati tra gli argomenti */
/* Restituisce la vecchia maschera */
sigset_t unblock_signals(int count, ...);

/* 
 * Imposta una maschera per i segnali, usata per reimpostare una
 * vecchia maschera ritornata da block_signals
 */
void reset_signals(sigset_t old_mask);

/* Imposta un nuovo handler per il segnale sig */
/* Ritorna il vecchio struct sigaction */
struct sigaction set_handler(int sig, void (*func)(int));

#endif /* __COMMON_H */
