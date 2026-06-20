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

int detect_operators(char *line, Pipeline *pipeline) {
    char *pos;
    char *start, *end;
    char saved;
    int found = 0;

    /* Producer <=: two syntaxes supported:
     *   "<= file command"  — <= at the start, file to the right
     *   "file <= command"  — <= in the middle, file to the left
     */
    pos = strstr(line, "<=");
    if(pos) {
        pipeline->has_producer = 1;
        if(pos == line) {
            /* <= file command */
            start = pos + 2;
            while(isspace((unsigned char)*start)) start++;
            end = start;
            while(*end && !isspace((unsigned char)*end)) end++;
            saved = *end; *end = '\0';
            pipeline->producer = strdup(start);
            *end = saved;
            memmove(line, end, strlen(end) + 1);
        } else {
            /* file <= command */
            char *cmd_start = pos + 2;
            while(isspace((unsigned char)*cmd_start)) cmd_start++;
            *pos = '\0';
            trim_whitespace(line);
            pipeline->producer = strdup(line);
            memmove(line, cmd_start, strlen(cmd_start) + 1);
        }
        trim_whitespace(line);
        found = 1;
    }

    /* Consumer => can appear anywhere after the commands */
    pos = strstr(line, "=>");
    if(pos) {
        pipeline->has_consumer = 1;
        start = pos + 2;
        while(isspace((unsigned char)*start)) start++;
        end = start;
        while(*end && !isspace((unsigned char)*end)) end++;
        saved = *end; *end = '\0';
        pipeline->consumer = strdup(start);
        *end = saved;
        *pos = '\0';
        trim_whitespace(line);
        found = 1;
    }

    return found;
}

int split_pipeline(char *line, char **commands, int max_commands) {
    int count = 0;
    char *start = line;
    char *end;
    char temp[MAX_LINE_LENGTH];

    while(*start && count < max_commands) {
        end = strchr(start, '|');

        if(end) {
            size_t len = end - start;
            if(len > 0 && len < MAX_LINE_LENGTH - 1) {
                strncpy(temp, start, len);
                temp[len] = '\0';
                trim_whitespace(temp);
                if(strlen(temp) > 0) {
                    commands[count++] = strdup(temp);
                }
            }
            start = end + 1;
        } else {
            trim_whitespace(start);
            if(strlen(start) > 0) {
                commands[count++] = strdup(start);
            }
            break;
        }
    }

    return count;
}

int parse_command(char *cmd_str, Command *cmd) {
    int argc = 0;
    char *start = cmd_str;
    char *end;
    char temp[MAX_LINE_LENGTH];

    memset(cmd, 0, sizeof(Command));
    cmd->background = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;

    trim_whitespace(cmd_str);
    if(strlen(cmd_str) == 0) {
        return -1;
    }

    while(*start && argc < MAX_ARGS - 1) {
        while(isspace((unsigned char)*start)) start++;
        if(*start == '\0') break;

        if(*start == '"' || *start == '\'') {
            char quote = *start++;
            end = start;
            while(*end && *end != quote) end++;
            size_t len = (size_t)(end - start);
            if(len < MAX_LINE_LENGTH - 1) {
                strncpy(temp, start, len);
                temp[len] = '\0';
                cmd->args[argc++] = strdup(temp);
            }
            if(*end == quote) end++;
            start = end;
            continue;
        }

        end = start;
        while(*end && !isspace((unsigned char)*end)) end++;

        size_t len = end - start;
        if(len > 0 && len < MAX_LINE_LENGTH - 1) {
            strncpy(temp, start, len);
            temp[len] = '\0';

            if(strcmp(temp, "<") == 0) {
                start = end;
                while(isspace((unsigned char)*start)) start++;
                end = start;
                while(*end && !isspace((unsigned char)*end)) end++;
                len = end - start;
                if(len > 0) {
                    strncpy(temp, start, len);
                    temp[len] = '\0';
                    cmd->input_file = strdup(temp);
                }
            }
            else if(strcmp(temp, ">") == 0) {
                start = end;
                while(isspace((unsigned char)*start)) start++;
                end = start;
                while(*end && !isspace((unsigned char)*end)) end++;
                len = end - start;
                if(len > 0) {
                    strncpy(temp, start, len);
                    temp[len] = '\0';
                    cmd->output_file = strdup(temp);
                }
            }
            else if(strcmp(temp, ">>") == 0) {
                start = end;
                while(isspace((unsigned char)*start)) start++;
                end = start;
                while(*end && !isspace((unsigned char)*end)) end++;
                len = end - start;
                if(len > 0) {
                    strncpy(temp, start, len);
                    temp[len] = '\0';
                    cmd->output_file = strdup(temp);
                }
            }
            else {
                cmd->args[argc++] = strdup(temp);
            }
        }

        start = end;
    }

    cmd->args[argc] = NULL;
    cmd->argc = argc;

    return 0;
}

int parse_line(char *line, Pipeline *pipeline) {
    char line_copy[MAX_LINE_LENGTH];
    char *commands[MAX_PIPES];
    int num_commands;

    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    memset(pipeline, 0, sizeof(Pipeline));
    pipeline->num_commands = 0;
    pipeline->background = 0;
    pipeline->has_producer = 0;
    pipeline->has_consumer = 0;
    pipeline->producer = NULL;
    pipeline->consumer = NULL;

    trim_whitespace(line_copy);
    size_t len = strlen(line_copy);
    if(len > 0 && line_copy[len-1] == '&') {
        pipeline->background = 1;
        line_copy[len-1] = '\0';
        trim_whitespace(line_copy);
    }

    detect_operators(line_copy, pipeline);

    num_commands = split_pipeline(line_copy, commands, MAX_PIPES);
    if(num_commands <= 0) {
        if(!pipeline->has_producer && !pipeline->has_consumer)
            return -1;
        pipeline->num_commands = 0;
        return 0;
    }

    pipeline->num_commands = num_commands;

    for(int i = 0; i < num_commands; i++) {
        if(parse_command(commands[i], &pipeline->commands[i]) < 0) {
            return -1;
        }
        free(commands[i]);
    }

    return 0;
}

void free_pipeline(Pipeline *pipeline) {
    if(!pipeline) return;

    for(int i = 0; i < pipeline->num_commands; i++) {
        Command *cmd = &pipeline->commands[i];
        if(cmd->input_file) {
            free(cmd->input_file);
            cmd->input_file = NULL;
        }
        if(cmd->output_file) {
            free(cmd->output_file);
            cmd->output_file = NULL;
        }
        for(int j = 0; j < cmd->argc; j++) {
            if(cmd->args[j]) {
                free(cmd->args[j]);
                cmd->args[j] = NULL;
            }
        }
    }

    if(pipeline->producer) {
        free(pipeline->producer);
        pipeline->producer = NULL;
    }

    if(pipeline->consumer) {
        free(pipeline->consumer);
        pipeline->consumer = NULL;
    }
}
