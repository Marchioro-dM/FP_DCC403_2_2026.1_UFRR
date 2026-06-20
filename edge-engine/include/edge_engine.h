#ifndef EDGE_ENGINE_H
#define EDGE_ENGINE_H

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

#define PROMPT "[edge-orchester] » "
#define MAX_LINE_LENGTH 4096
#define MAX_ARGS 64
#define MAX_PIPES 32
#define MAX_PATH 1024

typedef enum {
    MODE_INTERACTIVE,
    MODE_BATCH
} ExecutionMode;

typedef struct {
    char *args[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
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
} Pipeline;

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

#endif
