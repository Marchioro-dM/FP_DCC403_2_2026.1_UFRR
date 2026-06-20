#ifndef PARSER_H
#define PARSER_H

#include "edge_engine.h"

int parse_line(char *line, Pipeline *pipeline);
void trim_whitespace(char *str);
int split_pipeline(char *line, char **commands, int max_commands);
int parse_command(char *cmd_str, Command *cmd);
int detect_operators(char *line, Pipeline *pipeline);
void free_pipeline(Pipeline *pipeline);  /* Declaração */

#endif
