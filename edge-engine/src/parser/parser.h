#ifndef PARSER_H
#define PARSER_H

#include "edge_engine.h"

int parse_line(char *line, Pipeline *pipeline);
void trim_whitespace(char *str);

#endif
