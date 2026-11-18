#include "sys.h"
#include "ubase.h"

int main(int argc, char** argv, char** envp) {
    (void)envp;
    const char* dir = (argc > 1) ? argv[1] : ".";

    int fd = sys_opendir(dir);
    if (fd < 0) { print("ls: cannot open "); print(dir); print("\n"); return 1; }

    DirEntry e;
    for (uint32_t i = 0; ; i++) {
        if (sys_readdir(fd, i, &e) != 0) break;
        if (e.name[0] == '.') continue;

        print(e.name);
        if (e.attr & FS_ATTR_DIR) print("/");
        print("\n");
    }
    sys_close(fd);
    return 0;
}