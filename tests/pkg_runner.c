#include "../user/sys.h"

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    char* package_argv[] = {"PKG.ELF", "/PKGTEST.ZIP", 0};
    sys_exec("/bin/PKG.ELF", package_argv, envp);
    return 127;
}