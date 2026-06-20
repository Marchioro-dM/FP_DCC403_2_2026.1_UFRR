#include "executor.h"
#include "../builtins/builtins.h"
#include <sys/types.h>

int expand_wildcards(Command *cmd) {
    int new_argc = 0;
    char *new_args[MAX_ARGS] = {0};

    for(int i = 0; i < cmd->argc; i++) {
        if(strpbrk(cmd->args[i], "*?[") != NULL) {
            glob_t glob_result;
            int flags = GLOB_NOCHECK | GLOB_TILDE;

            if(glob(cmd->args[i], flags, NULL, &glob_result) == 0) {
                for(size_t j = 0; j < glob_result.gl_pathc && new_argc < MAX_ARGS - 1; j++) {
                    new_args[new_argc++] = strdup(glob_result.gl_pathv[j]);
                }
                globfree(&glob_result);
            } else {
                new_args[new_argc++] = strdup(cmd->args[i]);
            }
        } else {
            new_args[new_argc++] = strdup(cmd->args[i]);
        }
    }

    for(int i = 0; i < cmd->argc; i++) {
        cmd->args[i] = NULL;
    }
    for(int i = 0; i < new_argc; i++) {
        cmd->args[i] = new_args[i];
    }
    cmd->argc = new_argc;
    cmd->args[new_argc] = NULL;

    return 0;
}

int setup_pipes(int pipe_fds[][2], int num_pipes) {
    for(int i = 0; i < num_pipes; i++) {
        if(pipe(pipe_fds[i]) == -1) {
            perror("pipe");
            return -1;
        }
    }
    return 0;
}

void close_pipes(int pipe_fds[][2], int num_pipes) {
    for(int i = 0; i < num_pipes; i++) {
        if(pipe_fds[i][0] >= 0) close(pipe_fds[i][0]);
        if(pipe_fds[i][1] >= 0) close(pipe_fds[i][1]);
    }
}

int execute_command(Command *cmd, int in_fd, int out_fd) {
    if(cmd->argc == 0) return 0;

    /*
     * Builtins que participam de um pipeline precisam rodar num filho
     * para que os redirecionamentos de fd funcionem corretamente.
     * Só executa direto no pai quando não há pipe envolvido (ambos -1).
     */
    if(is_builtin(cmd) && in_fd < 0 && out_fd < 0
       && !cmd->input_file && !cmd->output_file) {
        return execute_builtin(cmd);
    }

    pid_t pid = fork();
    if(pid == -1) {
        perror("fork");
        return -1;
    }

    if(pid == 0) {
        /* Configura entrada */
        if(cmd->input_file) {
            int fd = open(cmd->input_file, O_RDONLY);
            if(fd == -1) {
                fprintf(stderr, "edge-engine: %s: %s\n", cmd->input_file, strerror(errno));
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        } else if(in_fd >= 0) {
            dup2(in_fd, STDIN_FILENO);
        }

        /* Configura saída */
        if(cmd->output_file) {
            int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd == -1) {
                fprintf(stderr, "edge-engine: %s: %s\n", cmd->output_file, strerror(errno));
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else if(out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
        }

        if(in_fd >= 0)  close(in_fd);
        if(out_fd >= 0) close(out_fd);

        /* Builtin dentro de pipe: executa e sai */
        if(is_builtin(cmd)) {
            int ret = execute_builtin(cmd);
            exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        /* Comando externo */
        expand_wildcards(cmd);
        execvp(cmd->args[0], cmd->args);

        if(errno == ENOENT) {
            fprintf(stderr, "edge-engine: comando não encontrado: %s\n", cmd->args[0]);
        } else if(errno == EACCES) {
            fprintf(stderr, "edge-engine: permissão negada: %s\n", cmd->args[0]);
        } else {
            fprintf(stderr, "edge-engine: erro ao executar %s: %s\n",
                    cmd->args[0], strerror(errno));
        }
        exit(EXIT_FAILURE);
    }

    return pid;
}

int execute_pipeline(Pipeline *pipeline) {
    if(pipeline->num_commands == 0) {
        if(pipeline->has_producer && pipeline->producer) {
            int pipe_fds[2];
            if(pipe(pipe_fds) == -1) { perror("pipe"); return -1; }

            pid_t pid = fork();
            if(pid == 0) {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
                execlp("cat", "cat", pipeline->producer, NULL);
                perror("execlp");
                exit(EXIT_FAILURE);
            }
            close(pipe_fds[1]);

            if(pipeline->has_consumer && pipeline->consumer) {
                int fd = open(pipeline->consumer, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(fd == -1) { perror("open consumer"); return -1; }
                char buffer[4096];
                ssize_t n;
                while((n = read(pipe_fds[0], buffer, sizeof(buffer))) > 0)
                    write(fd, buffer, n);
                close(fd);
            } else {
                char buffer[4096];
                ssize_t n;
                while((n = read(pipe_fds[0], buffer, sizeof(buffer))) > 0)
                    write(STDOUT_FILENO, buffer, n);
            }

            close(pipe_fds[0]);
            waitpid(pid, NULL, 0);
            return 0;
        }

        if(pipeline->has_consumer && pipeline->consumer) {
            int fd = open(pipeline->consumer, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd == -1) { perror("open consumer"); return -1; }
            char buffer[4096];
            ssize_t n;
            while((n = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
                write(fd, buffer, n);
            close(fd);
        }
        return 0;
    }

    int num_cmds  = pipeline->num_commands;
    int num_pipes = num_cmds - 1;
    int pipe_fds[MAX_PIPES][2];
    int producer_pipe[2] = {-1, -1};
    int consumer_pipe[2] = {-1, -1};
    pid_t pids[MAX_PIPES + 2]; /* +2 para produtor e consumidor */
    int pid_count = 0;

    if(num_pipes > 0) {
        if(setup_pipes(pipe_fds, num_pipes) < 0) return -1;
    }

    /* Pipe dedicado produtor→cmd[0] */
    if(pipeline->has_producer && pipeline->producer) {
        if(pipe(producer_pipe) == -1) { perror("pipe"); return -1; }
    }

    /* Pipe dedicado cmd[last]→consumidor */
    if(pipeline->has_consumer && pipeline->consumer) {
        if(pipe(consumer_pipe) == -1) { perror("pipe"); return -1; }
    }

    /* Produtor (<=) */
    if(pipeline->has_producer && pipeline->producer) {
        pid_t pid = fork();
        if(pid == 0) {
            close(producer_pipe[0]);
            dup2(producer_pipe[1], STDOUT_FILENO);
            close(producer_pipe[1]);
            for(int i = 0; i < num_pipes; i++) { close(pipe_fds[i][0]); close(pipe_fds[i][1]); }
            if(consumer_pipe[0] >= 0) { close(consumer_pipe[0]); close(consumer_pipe[1]); }
            execlp("cat", "cat", pipeline->producer, NULL);
            perror("execlp");
            exit(EXIT_FAILURE);
        }
        pids[pid_count++] = pid;
        close(producer_pipe[1]);
    }

    /* Comandos do pipeline */
    for(int i = 0; i < num_cmds; i++) {
        int in_fd  = -1;
        int out_fd = -1;

        if(i == 0 && pipeline->has_producer) {
            in_fd = producer_pipe[0];
        } else if(i > 0) {
            in_fd = pipe_fds[i-1][0];
        }

        if(i < num_cmds - 1) {
            out_fd = pipe_fds[i][1];
        } else if(pipeline->has_consumer) {
            out_fd = consumer_pipe[1];
        }

        int pid = execute_command(&pipeline->commands[i], in_fd, out_fd);
        if(pid > 0) pids[pid_count++] = pid;

        if(in_fd  >= 0) close(in_fd);
        if(out_fd >= 0) close(out_fd);
    }

    /* Consumidor (=>) */
    if(pipeline->has_consumer && pipeline->consumer) {
        pid_t pid = fork();
        if(pid == 0) {
            dup2(consumer_pipe[0], STDIN_FILENO);
            close(consumer_pipe[0]);
            close(consumer_pipe[1]);
            for(int i = 0; i < num_pipes; i++) { close(pipe_fds[i][0]); close(pipe_fds[i][1]); }
            if(producer_pipe[0] >= 0) close(producer_pipe[0]);
            int fd = open(pipeline->consumer, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd == -1) { perror("open consumer"); exit(EXIT_FAILURE); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            execlp("cat", "cat", NULL);
            perror("execlp");
            exit(EXIT_FAILURE);
        }
        pids[pid_count++] = pid;
        close(consumer_pipe[0]);
    }

    if(!pipeline->background) {
        for(int i = 0; i < pid_count; i++) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    } else {
        for(int i = 0; i < pid_count; i++) {
            printf("[%d] %d\n", i + 1, pids[i]);
        }
    }

    return 0;
}
