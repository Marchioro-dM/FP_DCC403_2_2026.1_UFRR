#include "parser.h"
#include <string.h>

void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = '\0';
}

int parse_line(char *line, Pipeline *pipeline) {
    Command *cmd = &pipeline->commands[0];

    trim_whitespace(line);
    if(strlen(line) == 0) return -1;

    char *token = strtok(line, " ");
    int argc = 0;

    while(token != NULL && argc < MAX_ARGS - 1) {
        cmd->args[argc] = token;
        argc++;
        token = strtok(NULL, " ");
    }
    cmd->args[argc] = NULL;
    cmd->argc = argc;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->background = 0;

    pipeline->num_commands = 1;
    pipeline->background = 0;
    pipeline->producer = NULL;
    pipeline->consumer = NULL;

    if(argc > 0 && strcmp(cmd->args[argc-1], "&") == 0) {
        cmd->args[argc-1] = NULL;
        cmd->argc--;
        pipeline->background = 1;
    }

    return 0;
}
