#include "utils.h"

void safe_close(int fd) {
    if(fd >= 0) {
        close(fd);
    }
}

int safe_dup2(int oldfd, int newfd) {
    if(oldfd == -1) return -1;
    if(dup2(oldfd, newfd) == -1) {
        perror("dup2");
        return -1;
    }
    return 0;
}

char* strdup_safe(const char *src) {
    if(!src) return NULL;
    char *dst = malloc(strlen(src) + 1);
    if(dst) {
        strcpy(dst, src);
    }
    return dst;
}
