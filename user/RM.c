#include "sys.h"

int main(int argc, char** argv, char** envp) {
    (void)envp;
    int recursive = 0;
    const char* path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'r') recursive = 1;
        else path = argv[i];
    }
    if (!path) { print("rm: no file\n"); return 1; }

    if (recursive) {
        if (sys_rmdir_r(path) != 0) { print("rm: failed\n"); return 1; }
    } else {
        if (sys_unlink(path) != 0) {
            if (sys_rmdir(path) != 0) { print("rm: failed\n"); return 1; }
        }
    }
    return 0;
}