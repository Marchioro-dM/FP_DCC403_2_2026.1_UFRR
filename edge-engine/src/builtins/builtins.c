#include "builtins.h"
#include <unistd.h>

int is_builtin(Command *cmd) {
    if(cmd->argc == 0) return 0;

    const char *builtins[] = {"cd", "pwd", "echo", "exit", "fim"};
    for(int i = 0; i < 5; i++) {
        if(strcmp(cmd->args[0], builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int execute_builtin(Command *cmd) {
    if(strcmp(cmd->args[0], "cd") == 0) return builtin_cd(cmd);
    if(strcmp(cmd->args[0], "pwd") == 0) return builtin_pwd(cmd);
    if(strcmp(cmd->args[0], "echo") == 0) return builtin_echo(cmd);
    if(strcmp(cmd->args[0], "exit") == 0) return builtin_exit(cmd);
    if(strcmp(cmd->args[0], "fim") == 0) return builtin_exit(cmd);
    
    return -1;
}

int builtin_cd(Command *cmd) {
    const char *path = (cmd->argc > 1) ? cmd->args[1] : getenv("HOME");
    if(chdir(path) != 0) {
        perror("cd");
        return -1;
    }
    return 0;
}

int builtin_pwd(Command *cmd) {
    (void)cmd;  /* Parâmetro não utilizado */
    char cwd[MAX_PATH];
    if(getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
        return -1;
    }
    return 0;
}

int builtin_echo(Command *cmd) {
    for(int i = 1; i < cmd->argc; i++) {
        printf("%s", cmd->args[i]);
        if(i < cmd->argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

int builtin_exit(Command *cmd) {
    (void)cmd;  /* Parâmetro não utilizado */
    exit(0);
    return 0; /* Nunca alcançado */
}
