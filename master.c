#define _GNU_SOURCE

#include <stdio.h>      /* printf(), fgets() */
#include <stdlib.h>     /* atoi(), calloc(), free(), getenv() */ 
#include <limits.h>     /* Limits of numbers macros */ 
#include <string.h>     /* stderr */
#include <errno.h>      /* errno */
#include "bashprint.h"  /* Pretty print messages to screen */

#define N_RUNTIME_CONF_VALUES 13

/* RUN TIME CONFIGURATION VALUES */
unsigned long conf[N_RUNTIME_CONF_VALUES] = {0};
enum conf_index {
			SO_USERS_NUM, SO_NODES_NUM, SO_BUDGET_INIT, SO_REWARD, 
			SO_MIN_TRANS_GEN_NSEC, SO_MAX_TRANS_GEN_NSEC, SO_RETRY, 
			SO_TP_SIZE, SO_MIN_TRANS_PROC_NSEC, SO_MAX_TRANS_PROC_NSEC, 
			SO_SIM_SEC, SO_FRIENDS_NUM, SO_HOPS
		};
char conf_names[N_RUNTIME_CONF_VALUES][22+1] = {
			"SO_USERS_NUM", "SO_NODES_NUM", "SO_BUDGET_INIT", "SO_REWARD",
			"SO_MIN_TRANS_GEN_NSEC", "SO_MAX_TRANS_GEN_NSEC", "SO_RETRY",
			"SO_TP_SIZE", "SO_MIN_TRANS_PROC_NSEC", "SO_MAX_TRANS_PROC_NSEC", 
			"SO_SIM_SEC", "SO_FRIENDS_NUM", "SO_HOPS"
		    };

void get_configuration();

int main (int argc, char ** argv)
{
	/*system("clear"); */
	get_configuration();
	return 0;
}

void get_configuration()
{
	char *env_var_val = NULL;    /* used to read env variables */
	char *chk_strtol_err = NULL; /* used to detect errors when converting str to ulong */
	char i = 0;
#ifdef DEBUG
	MSG_INFO("Checking compile time parameters...");
#endif
	/* Check compile time parameters */
#ifndef SO_BLOCK_SIZE
	MSG_ERR("SO_BLOCK_SIZE is not defined!");
	exit(EXIT_FAILURE);
#endif
#ifndef SO_REGISTRY_SIZE
	MSG_ERR("SO_REGISTRY_SIZE is not defined!");
	exit(EXIT_FAILURE);
#endif
#ifdef DEBUG
	printf("---------------------------------------------------\n");
	printf("|                   COMPILE TIME                  |\n");
	printf("---------------------------------------------------\n");
	printf("|    SO_BLOCK_SIZE             |    %10u    |\n", SO_BLOCK_SIZE);
	printf("|    SO_REGISTRY_SIZE          |    %10u    |\n", SO_REGISTRY_SIZE);
	printf("---------------------------------------------------\n");
	MSG_OK("Compile time parameters exist!");
	MSG_INFO("Checking run time parameters...");
	printf("---------------------------------------------------\n");
	printf("|                   RUNNING TIME                  |\n");
	printf("---------------------------------------------------\n");
#endif
	for (i = SO_USERS_NUM; i < N_RUNTIME_CONF_VALUES; i++) {
		if(env_var_val = getenv(conf_names[i])){
			errno = 0;
			conf[i] = strtoul(env_var_val, &chk_strtol_err, 10);
			/* check conversion */
			if((conf[i] == 0 && (errno == EINVAL || errno == ERANGE)) || env_var_val == chk_strtol_err) {
				fprintf(stderr, "[%sERROR%s] Could not convert env variable %s to an unsigned long\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
				exit(EXIT_FAILURE);
			}
			/* check valid number */
			if(i == SO_MAX_TRANS_GEN_NSEC && conf[SO_MAX_TRANS_GEN_NSEC] < conf[SO_MIN_TRANS_GEN_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_GEN_NSEC is lower than SO_MIN_TRANS_GEN_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_MAX_TRANS_PROC_NSEC && conf[SO_MAX_TRANS_PROC_NSEC] < conf[SO_MIN_TRANS_PROC_NSEC]) {
				MSG_ERR("SO_MAX_TRANS_PROC_NSEC is lower than SO_MIN_TRANS_PROC_NSEC!");
				exit(EXIT_FAILURE);
			} else if(i == SO_TP_SIZE && conf[SO_TP_SIZE] <= SO_BLOCK_SIZE) {
				MSG_ERR("SO_TP_SIZE is not bigger than SO_BLOCK_SIZE!");
				exit(EXIT_FAILURE);
			} else if (i == SO_REWARD && (conf[SO_REWARD] < 0 || conf[SO_REWARD] > 100)) {
				MSG_ERR("SO_REWARD is out range [0-100]!");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "[%sERROR%s] Undefined environment variable %s. Make sure to load env. variables first!\n",
						COLOR_RED, COLOR_FLUSH, conf_names[i]);
			exit(EXIT_FAILURE);
		}
	}
#ifdef DEBUG
	i = SO_USERS_NUM;
	printf("|    SO_USERS_NUM              |    %10u    |\n", conf[i++]);
	printf("|    SO_NODES_NUM              |    %10u    |\n", conf[i++]);
	printf("|    SO_BUDGET_INIT            |    %10u    |\n", conf[i++]);
	printf("|    SO_REWARD                 |    %10u    |\n", conf[i++]);
	printf("|    SO_MIN_TRANS_GEN_NSEC     |    %10u    |\n", conf[i++]);
	printf("|    SO_MAX_TRANS_GEN_NSEC     |    %10u    |\n", conf[i++]);
	printf("|    SO_RETRY                  |    %10u    |\n", conf[i++]);
	printf("|    SO_TP_SIZE                |    %10u    |\n", conf[i++]);
	printf("|    SO_MIN_TRANS_PROC_NSEC    |    %10u    |\n", conf[i++]);
	printf("|    SO_MAX_TRANS_PROC_NSEC    |    %10u    |\n", conf[i++]);
	printf("|    SO_SIM_SEC                |    %10u    |\n", conf[i++]);
	printf("|    SO_FRIENDS_NUM            |    %10u    |\n", conf[i++]);
	printf("|    SO_HOPS                   |    %10u    |\n", conf[i]);
	printf("---------------------------------------------------\n");
	MSG_OK("Running time parameters retrieved successfully!");
#endif
}