#include "executor.h"
#include "../builtins/builtins.h"
#include <sys/types.h>

/* Monta o nome canônico de uma fila POSIX: "/nome" (sem outras barras). */
static void mq_canonical_name(const char *name, char *out, size_t out_sz) {
    snprintf(out, out_sz, "/%s", name);
}

static struct mq_attr mq_default_attr(void) {
    struct mq_attr attr;
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = MQ_MAX_MSG;
    attr.mq_msgsize = MQ_MSG_SIZE;
    attr.mq_curmsgs = 0;
    return attr;
}

/*
 * Produtor (<=): drena a fila POSIX nomeada e escreve as mensagens em out_fd
 * (que alimenta o stdin do pipeline). A leitura é não-bloqueante: quando a
 * fila esvazia (EAGAIN), o dreno termina. A fila é removida ao final
 * (mq_unlink) para não desperdiçar recursos do SO.
 */
static int run_mq_producer(const char *name, int out_fd) {
    char qname[256];
    mq_canonical_name(name, qname, sizeof(qname));

    struct mq_attr attr = mq_default_attr();
    mqd_t mq = mq_open(qname, O_RDONLY | O_CREAT | O_NONBLOCK, 0644, &attr);
    if(mq == (mqd_t)-1) {
        fprintf(stderr, "edge-engine: mq_open(%s): %s\n", qname, strerror(errno));
        return -1;
    }

    char buf[MQ_MSG_SIZE];
    ssize_t n;
    while((n = mq_receive(mq, buf, MQ_MSG_SIZE, NULL)) >= 0) {
        ssize_t off = 0;
        while(off < n) {
            ssize_t w = write(out_fd, buf + off, (size_t)(n - off));
            if(w <= 0) break;
            off += w;
        }
    }
    /* n < 0 com errno==EAGAIN significa fila vazia: fim normal do dreno. */

    mq_close(mq);
    mq_unlink(qname);
    return 0;
}

/*
 * Consumidor (=>): lê de in_fd (stdout do pipeline) e publica os dados como
 * mensagens na fila POSIX nomeada. Cada bloco lido vira uma mensagem.
 */
static int run_mq_consumer(const char *name, int in_fd) {
    char qname[256];
    mq_canonical_name(name, qname, sizeof(qname));

    struct mq_attr attr = mq_default_attr();
    mqd_t mq = mq_open(qname, O_WRONLY | O_CREAT, 0644, &attr);
    if(mq == (mqd_t)-1) {
        fprintf(stderr, "edge-engine: mq_open(%s): %s\n", qname, strerror(errno));
        return -1;
    }

    char buf[MQ_MSG_SIZE];
    ssize_t n;
    while((n = read(in_fd, buf, MQ_MSG_SIZE)) > 0) {
        if(mq_send(mq, buf, (size_t)n, 0) == -1) {
            fprintf(stderr, "edge-engine: mq_send(%s): %s\n", qname, strerror(errno));
            mq_close(mq);
            return -1;
        }
    }

    mq_close(mq);
    return 0;
}

/*
 * Ponte fila→fila usada quando <= e => aparecem sem comandos entre eles
 * (ex.: "<= origem => destino"): drena a fila de origem e republica na de destino.
 */
static int run_mq_bridge(const char *src, const char *dst) {
    char sq[256], dq[256];
    mq_canonical_name(src, sq, sizeof(sq));
    mq_canonical_name(dst, dq, sizeof(dq));

    struct mq_attr attr = mq_default_attr();
    mqd_t in  = mq_open(sq, O_RDONLY | O_CREAT | O_NONBLOCK, 0644, &attr);
    mqd_t out = mq_open(dq, O_WRONLY | O_CREAT, 0644, &attr);
    if(in == (mqd_t)-1 || out == (mqd_t)-1) {
        fprintf(stderr, "edge-engine: mq_open ponte: %s\n", strerror(errno));
        if(in  != (mqd_t)-1) mq_close(in);
        if(out != (mqd_t)-1) mq_close(out);
        return -1;
    }

    char buf[MQ_MSG_SIZE];
    ssize_t n;
    while((n = mq_receive(in, buf, MQ_MSG_SIZE, NULL)) >= 0) {
        if(mq_send(out, buf, (size_t)n, 0) == -1) break;
    }

    mq_close(in);
    mq_close(out);
    mq_unlink(sq);
    return 0;
}

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
        free(cmd->args[i]);
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
        fcntl(pipe_fds[i][0], F_SETFD, FD_CLOEXEC);
        fcntl(pipe_fds[i][1], F_SETFD, FD_CLOEXEC);
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

    fflush(NULL);
    pid_t pid = fork();
    if(pid == -1) {
        perror("fork");
        return -1;
    }

    if(pid == 0) {
        /* Restaura sinais para o padrão: o pai ignora SIGINT/SIGQUIT, mas o
         * filho (em foreground) deve poder ser interrompido via Ctrl-C/Ctrl-\. */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        /* Configura entrada */
        if(cmd->input_file) {
            int fd = open(cmd->input_file, O_RDONLY);
            if(fd == -1) {
                fprintf(stderr, "edge-engine: %s: %s\n", cmd->input_file, strerror(errno));
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            /* fd fechado pelo close_range abaixo */
        } else if(in_fd >= 0) {
            dup2(in_fd, STDIN_FILENO);
            /* in_fd fechado pelo close_range abaixo */
        }

        /* Configura saída */
        if(cmd->output_file) {
            int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
            int fd = open(cmd->output_file, flags, 0644);
            if(fd == -1) {
                fprintf(stderr, "edge-engine: %s: %s\n", cmd->output_file, strerror(errno));
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            /* fd fechado pelo close_range abaixo */
        } else if(out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
            /* out_fd fechado pelo close_range abaixo */
        }

        /* Fechar todos os descritores herdados acima de stderr de uma vez.
         * Cobre pipes extras do pipeline em builtins (sem exec) e garante
         * que nenhum FD vaze, sem gerar double-close. */
        close_range(3, ~0U, 0);

        /* Builtin dentro de pipe: executa e sai */
        if(is_builtin(cmd)) {
            int ret = execute_builtin(cmd);
            fflush(stdout);
            close(STDOUT_FILENO);
            for(int i = 0; i < cmd->argc; i++) { free(cmd->args[i]); cmd->args[i] = NULL; }
            exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        /* Comando externo */
        expand_wildcards(cmd);
        execvp(cmd->args[0], cmd->args);

        /* execvp só retorna em caso de erro */
        if(errno == ENOENT) {
            fprintf(stderr, "edge-engine: comando não encontrado: %s\n", cmd->args[0]);
        } else if(errno == EACCES) {
            fprintf(stderr, "edge-engine: permissão negada: %s\n", cmd->args[0]);
        } else {
            fprintf(stderr, "edge-engine: erro ao executar %s: %s\n",
                    cmd->args[0], strerror(errno));
        }
        for(int i = 0; i < cmd->argc; i++) { free(cmd->args[i]); cmd->args[i] = NULL; }
        exit(errno == ENOENT ? 127 : errno == EACCES ? 126 : EXIT_FAILURE);
    }

    return pid;
}

int execute_pipeline(Pipeline *pipeline) {
    if(pipeline->num_commands == 0) {
        if(pipeline->has_producer && pipeline->producer) {
            if(pipeline->has_consumer && pipeline->consumer) {
                /* <= origem => destino : ponte direta fila→fila */
                return run_mq_bridge(pipeline->producer, pipeline->consumer);
            }
            /* <= origem : drena a fila para o stdout */
            return run_mq_producer(pipeline->producer, STDOUT_FILENO);
        }

        if(pipeline->has_consumer && pipeline->consumer) {
            /* => destino : envia o stdin para a fila */
            return run_mq_consumer(pipeline->consumer, STDIN_FILENO);
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

    /*
     * Bloqueia SIGCHLD antes de qualquer fork (foreground E background).
     *  - Foreground: o handler poderia recolher o filho (waitpid -1) antes do
     *    waitpid(pid) aqui, causando ECHILD e perdendo o exit status.
     *  - Background: o handler poderia recolher e marcar um pid antes de o job
     *    ser registrado em add_job, deixando o job "preso" como em execução.
     * O bloqueio é mantido até o waitpid (fg) ou até registrar o job (bg).
     */
    sigset_t block_mask, old_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_mask, &old_mask);

    if(num_pipes > 0) {
        if(setup_pipes(pipe_fds, num_pipes) < 0) {
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return -1;
        }
    }

    /* Pipe dedicado produtor→cmd[0] */
    if(pipeline->has_producer && pipeline->producer) {
        if(pipe(producer_pipe) == -1) {
            perror("pipe");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return -1;
        }
        fcntl(producer_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(producer_pipe[1], F_SETFD, FD_CLOEXEC);
    }

    /* Pipe dedicado cmd[last]→consumidor */
    if(pipeline->has_consumer && pipeline->consumer) {
        if(pipe(consumer_pipe) == -1) {
            perror("pipe");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return -1;
        }
        fcntl(consumer_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(consumer_pipe[1], F_SETFD, FD_CLOEXEC);
    }

    /* Produtor (<=) */
    if(pipeline->has_producer && pipeline->producer) {
        fflush(NULL);
        pid_t pid = fork();
        if(pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            close(producer_pipe[0]);
            for(int i = 0; i < num_pipes; i++) { close(pipe_fds[i][0]); close(pipe_fds[i][1]); }
            if(consumer_pipe[0] >= 0) { close(consumer_pipe[0]); close(consumer_pipe[1]); }
            int rc = run_mq_producer(pipeline->producer, producer_pipe[1]);
            close(producer_pipe[1]);
            _exit(rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
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
        fflush(NULL);
        pid_t pid = fork();
        if(pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            close(consumer_pipe[1]);
            for(int i = 0; i < num_pipes; i++) { close(pipe_fds[i][0]); close(pipe_fds[i][1]); }
            if(producer_pipe[0] >= 0) close(producer_pipe[0]);
            int rc = run_mq_consumer(pipeline->consumer, consumer_pipe[0]);
            close(consumer_pipe[0]);
            _exit(rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }
        pids[pid_count++] = pid;
        close(consumer_pipe[0]);
    }

    if(!pipeline->background) {
        int last_status = 0;
        for(int i = 0; i < pid_count; i++) {
            int status = 0;
            if(waitpid(pids[i], &status, 0) > 0) {
                if(WIFEXITED(status))
                    last_status = WEXITSTATUS(status);
                else if(WIFSIGNALED(status))
                    last_status = 128 + WTERMSIG(status);
            }
        }
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        return last_status;
    } else {
        /* Registra o pipeline como job e notifica o lançamento. O job é
         * registrado ANTES de desbloquear SIGCHLD, evitando que o handler
         * recolha um filho antes de o job existir na tabela. */
        if(pid_count > 0) {
            int jid = add_job(pids, pid_count, pipeline->raw ? pipeline->raw : "");
            if(jid > 0)
                printf("[%d] %d\n", jid, pids[pid_count - 1]);
        }
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
    }

    return 0;
}
