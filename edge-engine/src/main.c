#include "edge_engine.h"
#include "parser/parser.h"
#include "executor/executor.h"
#include "builtins/builtins.h"

volatile sig_atomic_t child_status = 0;
pid_t foreground_pid = -1;

void handle_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    int status;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if(pid == foreground_pid) {
            foreground_pid = -1;
        }
        child_status = status;
    }

    errno = saved_errno;
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

    char *line;
    while((line = read_line(input, mode == MODE_INTERACTIVE)) != NULL) {
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

        if(execute_pipeline(&pipeline) < 0) {
            fprintf(stderr, "Erro ao executar pipeline\n");
        }

        free_pipeline(&pipeline);
    }

    if(mode == MODE_BATCH) {
        fclose(input);
    }

    printf("\nEncerrando edge-engine...\n");
    return EXIT_SUCCESS;
}
