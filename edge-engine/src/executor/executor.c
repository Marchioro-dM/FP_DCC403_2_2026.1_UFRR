#include "executor.h"
#include "../builtins/builtins.h"  /* Caminho relativo corrigido */
#include <sys/types.h>

int execute_command(Command *cmd) {
    if(cmd->argc == 0) return 0;

    /* Verifica se é built-in */
    if(is_builtin(cmd)) {
        return execute_builtin(cmd);
    }

    /* Fork e exec */
    pid_t pid = fork();

    if(pid == -1) {
        perror("fork");
        return -1;
    }

    if(pid == 0) {
        /* Processo filho */
        if(cmd->input_file) {
            int fd = open(cmd->input_file, O_RDONLY);
            if(fd == -1) {
                perror("open input");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if(cmd->output_file) {
            int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd == -1) {
                perror("open output");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(cmd->args[0], cmd->args);
        perror("execvp");
        exit(EXIT_FAILURE);
    }

    if(!cmd->background) {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    } else {
        printf("[%d] %d\n", cmd->argc, pid);
        return 0;
    }
}

int execute_pipeline(Pipeline *pipeline) {
    if(pipeline->num_commands == 0) return 0;

    int ret = 0;
    for(int i = 0; i < pipeline->num_commands; i++) {
        int result = execute_command(&pipeline->commands[i]);
        if(result != 0) {
            ret = result;
        }
    }

    return ret;
}
