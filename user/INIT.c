#include "sys.h"

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv;
    char* new_argv[] = { "sh", "/etc/init.sh", NULL };
    sys_exec("/bin/SH.ELF", new_argv, envp);
    print("init: exec SH failed\n");
    return 1;
}