#ifndef BUILTINS_H
#define BUILTINS_H

#include "edge_engine.h"

int is_builtin(Command *cmd);
int execute_builtin(Command *cmd);

/* Comandos built-in */
int builtin_cd(Command *cmd);
int builtin_pwd(Command *cmd);
int builtin_echo(Command *cmd);
int builtin_exit(Command *cmd);
int builtin_jobs(Command *cmd);

#endif
