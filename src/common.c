#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include "common.h"

int sem_signal(int s_id, unsigned short sem_num) {
	struct sembuf sops;
	sops.sem_flg = 0;
	sops.sem_op = 1;
	sops.sem_num = sem_num;
	return semop(s_id, &sops, 1);
}

int sem_wait(int s_id, unsigned short sem_num) {
	struct sembuf sops;
	sops.sem_flg = 0;
	sops.sem_op = -1;
	sops.sem_num = sem_num;
	return semop(s_id, &sops, 1);
}

int sem_cmd(int s_id, unsigned short sem_num, short sem_op, short sem_flg) {
	struct sembuf sops;
	sops.sem_flg = sem_flg;
	sops.sem_op = sem_op;
	sops.sem_num = sem_num;
	return semop(s_id, &sops, 1);
}

sigset_t block_signals(int count, ...) {
	sigset_t mask, old_mask;
	va_list argptr;
	int i;

	sigemptyset(&mask);

	va_start(argptr, count);

	for (i = 0; i < count; i++) {
		sigaddset(&mask, va_arg(argptr, int));
	}

	va_end(argptr);

	sigprocmask(SIG_BLOCK, &mask, &old_mask);
	return old_mask;
}

sigset_t unblock_signals(int count, ...) {
	sigset_t mask, old_mask;
	va_list argptr;
	int i;

	sigemptyset(&mask);

	va_start(argptr, count);

	for (i = 0; i < count; i++) {
		sigaddset(&mask, va_arg(argptr, int));
	}

	va_end(argptr);

	sigprocmask(SIG_UNBLOCK, &mask, &old_mask);
	return old_mask;
}

void reset_signals(sigset_t old_mask) {
	sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

struct sigaction set_handler(int sig, void (*func)(int)) {
	struct sigaction sa, sa_old;
	sigset_t mask;
	sigemptyset(&mask);
	sa.sa_handler = func;
	sa.sa_mask = mask;
	sa.sa_flags = 0;
	sigaction(sig, &sa, &sa_old);
	return sa_old;
}