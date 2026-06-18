#ifndef UTILS_H
#define UTILS_H

#include "edge_engine.h"

/* Funções utilitárias */
void safe_close(int fd);
int safe_dup2(int oldfd, int newfd);
char* strdup_safe(const char *src);

#endif
