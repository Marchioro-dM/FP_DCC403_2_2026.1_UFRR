#ifndef EDGE_ENGINE_H
#define EDGE_ENGINE_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <glob.h>
#include <errno.h>
#include <ctype.h>
#include <mqueue.h>

#define PROMPT "[edge-orchester] » "
#define MAX_LINE_LENGTH 4096
#define MAX_ARGS 64
#define MAX_PIPES 32
#define MAX_PATH 1024

/* Filas de mensagens POSIX usadas pelos operadores <= (produtor) e => (consumidor).
 * Limites compatíveis com os defaults do kernel (msg_max=10, msgsize_max=8192),
 * evitando a necessidade de privilégios para criar as filas. */
#define MQ_MAX_MSG 10
#define MQ_MSG_SIZE 8192

typedef enum {
    MODE_INTERACTIVE,
    MODE_BATCH
} ExecutionMode;

typedef struct {
    char *args[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
    int append;        /* 1 se a saída usa >> (append), 0 se > (truncate) */
    int background;
} Command;

typedef struct {
    Command commands[MAX_PIPES];
    int num_commands;
    int background;
    char *producer;   /* Nome do produtor (para <=) */
    char *consumer;   /* Nome do consumidor (para =>) */
    int has_producer; /* Flag se tem produtor */
    int has_consumer; /* Flag se tem consumidor */
    char *raw;        /* Linha original (rótulo de jobs em background) */
} Pipeline;

#define MAX_JOBS 64
#define MAX_JOB_PIDS (MAX_PIPES + 2)

/* Job em background: pode agregar vários processos (um pipeline inteiro). */
typedef struct {
    int   used;        /* slot ocupado */
    int   job_id;      /* número [n] exibido ao usuário */
    pid_t pids[MAX_JOB_PIDS];
    int   npids;
    int   nremaining;  /* processos do job ainda em execução */
    int   last_status; /* status do último processo a terminar */
    char  cmd[MAX_LINE_LENGTH];
} Job;

extern volatile sig_atomic_t child_status;
extern pid_t foreground_pid;

void handle_sigchld(int sig);
void setup_signal_handlers(void);
char* read_line(FILE *input, int interactive);
int parse_line(char *line, Pipeline *pipeline);
int execute_pipeline(Pipeline *pipeline);
void free_pipeline(Pipeline *pipeline);
int is_builtin(Command *cmd);
int execute_builtin(Command *cmd);

/* Controle de jobs em background (definido em main.c). */
int  add_job(pid_t *pids, int npids, const char *cmd);
void report_done_jobs(void);
void list_jobs(void);

#endif
