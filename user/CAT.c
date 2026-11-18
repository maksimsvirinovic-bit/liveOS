#include "sys.h"
#include "ubase.h"

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) { print("cat: no file\n"); return 1; }
    int fd = sys_open(argv[1], 0);
    if (fd < 0) { print("cat: cannot open\n"); return 1; }

    char buf[512];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        sys_write(1, buf, n);
    }
    sys_close(fd);
    return 0;
}