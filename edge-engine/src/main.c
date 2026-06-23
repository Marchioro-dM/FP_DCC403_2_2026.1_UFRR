#include "edge_engine.h"
#include "parser/parser.h"
#include "executor/executor.h"
#include "builtins/builtins.h"

volatile sig_atomic_t child_status = 0;
pid_t foreground_pid = -1;

static Job job_table[MAX_JOBS];

/* Bloqueia/desbloqueia SIGCHLD para proteger a tabela de jobs contra acesso
 * concorrente entre o loop principal e o handler. */
static void block_sigchld(sigset_t *old) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, old);
}
static void restore_sigmask(sigset_t *old) {
    sigprocmask(SIG_SETMASK, old, NULL);
}

void handle_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    int status;

    /* Apenas operações async-signal-safe: waitpid e escrita em inteiros. */
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if(pid == foreground_pid) {
            foreground_pid = -1;
        }
        child_status = status;

        for(int j = 0; j < MAX_JOBS; j++) {
            if(!job_table[j].used) continue;
            for(int k = 0; k < job_table[j].npids; k++) {
                if(job_table[j].pids[k] == pid) {
                    if(job_table[j].nremaining > 0) job_table[j].nremaining--;
                    job_table[j].last_status = status;
                    break;
                }
            }
        }
    }

    errno = saved_errno;
}

/* Registra um pipeline em background na tabela. O chamador (execute_pipeline)
 * já mantém SIGCHLD bloqueado; ainda assim protegemos por segurança. */
int add_job(pid_t *pids, int npids, const char *cmd) {
    sigset_t old;
    block_sigchld(&old);

    int slot = -1, max_id = 0;
    for(int j = 0; j < MAX_JOBS; j++) {
        if(job_table[j].used) {
            if(job_table[j].job_id > max_id) max_id = job_table[j].job_id;
        } else if(slot < 0) {
            slot = j;
        }
    }
    if(slot < 0) { restore_sigmask(&old); return -1; }

    Job *job = &job_table[slot];
    job->used = 1;
    job->job_id = max_id + 1;
    job->npids = (npids > MAX_JOB_PIDS) ? MAX_JOB_PIDS : npids;
    job->nremaining = job->npids;
    job->last_status = 0;
    for(int k = 0; k < job->npids; k++) job->pids[k] = pids[k];
    strncpy(job->cmd, cmd ? cmd : "", MAX_LINE_LENGTH - 1);
    job->cmd[MAX_LINE_LENGTH - 1] = '\0';

    int id = job->job_id;
    restore_sigmask(&old);
    return id;
}

/* Imprime e remove os jobs já concluídos. Chamado no loop antes do prompt. */
void report_done_jobs(void) {
    sigset_t old;
    block_sigchld(&old);

    for(int j = 0; j < MAX_JOBS; j++) {
        if(!job_table[j].used || job_table[j].nremaining > 0) continue;

        int st = job_table[j].last_status;
        if(WIFEXITED(st) && WEXITSTATUS(st) != 0)
            printf("[%d]+ Exit %-3d %s\n", job_table[j].job_id, WEXITSTATUS(st), job_table[j].cmd);
        else if(WIFSIGNALED(st))
            printf("[%d]+ Encerrado (sinal %d) %s\n", job_table[j].job_id, WTERMSIG(st), job_table[j].cmd);
        else
            printf("[%d]+ Concluido  %s\n", job_table[j].job_id, job_table[j].cmd);

        job_table[j].used = 0;
    }
    fflush(stdout);

    restore_sigmask(&old);
}

/* Built-in jobs: lista os jobs em background ainda em execução. */
void list_jobs(void) {
    sigset_t old;
    block_sigchld(&old);

    int any = 0;
    for(int j = 0; j < MAX_JOBS; j++) {
        if(job_table[j].used && job_table[j].nremaining > 0) {
            printf("[%d]+ Executando  %s\n", job_table[j].job_id, job_table[j].cmd);
            any = 1;
        }
    }
    if(!any) printf("Nenhum job em background.\n");
    fflush(stdout);

    restore_sigmask(&old);
}

void setup_signal_handlers(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sa.sa_handler = handle_sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
}

char* read_line(FILE *input, int interactive) {
    static char buffer[MAX_LINE_LENGTH];

    if(interactive) {
        printf("%s", PROMPT);
        fflush(stdout);
    }

    if(fgets(buffer, sizeof(buffer), input) == NULL) {
        return NULL;
    }

    size_t len = strlen(buffer);
    if(len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
    }

    return buffer;
}

int main(int argc, char *argv[]) {
    ExecutionMode mode = MODE_INTERACTIVE;
    FILE *input = stdin;

    setup_signal_handlers();

    if(argc > 1) {
        mode = MODE_BATCH;
        input = fopen(argv[1], "r");
        if(!input) {
            perror("fopen");
            return EXIT_FAILURE;
        }
        printf("Modo batch: executando %s\n", argv[1]);
    }

    /*
     * Desabilita o buffer do stream de entrada. Com buffering padrão, quando a
     * entrada não é um terminal (pipe/arquivo), o fgets em read_line consome um
     * bloco inteiro do stdin de uma vez, "roubando" os dados que um comando
     * filho (ex.: `grep` lendo stdin) deveria receber pelo fd 0. Sem buffer, o
     * fgets lê só até o '\n' e deixa o restante disponível no fd para o filho.
     */
    setvbuf(input, NULL, _IONBF, 0);

    char *line;
    for(;;) {
        report_done_jobs();
        line = read_line(input, mode == MODE_INTERACTIVE);
        if(line == NULL) break;

        if(strcmp(line, "fim") == 0 || strcmp(line, "exit") == 0) {
            break;
        }

        if(strlen(line) == 0) {
            continue;
        }

        Pipeline pipeline = {0};
        if(parse_line(line, &pipeline) < 0) {
            fprintf(stderr, "Erro de sintaxe\n");
            continue;
        }

        int exit_status = execute_pipeline(&pipeline);
        if(exit_status < 0) {
            fprintf(stderr, "edge-engine: erro interno ao configurar pipeline\n");
        }

        free_pipeline(&pipeline);
    }

    if(mode == MODE_BATCH) {
        fclose(input);
    }

    printf("\nEncerrando edge-engine...\n");
    return EXIT_SUCCESS;
}
