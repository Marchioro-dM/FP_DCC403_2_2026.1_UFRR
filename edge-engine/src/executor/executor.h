#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "edge_engine.h"

int execute_pipeline(Pipeline *pipeline);
int execute_command(Command *cmd, int in_fd, int out_fd);
int execute_with_pipes(Command *cmds, int num_cmds, int background);
int setup_pipes(int pipe_fds[][2], int num_pipes);
void close_pipes(int pipe_fds[][2], int num_pipes);

#endif
