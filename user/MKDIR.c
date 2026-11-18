#include "sys.h"
#include "ubase.h"

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) { print("mkdir: no dir\n"); return 1; }
    if (sys_mkdir(argv[1]) != 0) { print("mkdir: failed\n"); return 1; }
    return 0;
}