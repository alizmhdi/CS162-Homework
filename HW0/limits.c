#include <stdio.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;
    struct rlimit *plim = &lim;
    
    getrlimit(RLIMIT_STACK , plim);
    printf("stack size: %ld\n", lim.rlim_cur);
    getrlimit(RLIMIT_NPROC , plim);
    printf("process limit: %ld\n", lim.rlim_cur);
    getrlimit(RLIMIT_NOFILE , plim);
    printf("max file descriptors: %ld\n", lim.rlim_cur);
    return 0;
}
