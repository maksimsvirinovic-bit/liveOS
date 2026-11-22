#include "sys.h"

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) { print("rmdir: no dir\n"); return 1; }
    if (sys_rmdir(argv[1]) != 0) { print("rmdir: failed\n"); return 1; }
    return 0;
}