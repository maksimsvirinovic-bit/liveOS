#include "sys.h"
#include "ubase.h"

static void resolve_cmd(const char* name, char* out) {
    if (name[0] == '/' || name[0] == '.') {
        strcpy_(out, name);
        return;
    }
    out[0] = '/'; out[1] = 'b'; out[2] = 'i'; out[3] = 'n'; out[4] = '/';
    int i = 0;
    while (name[i] && i < 240) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[5 + i] = c;
        i++;
    }
    if (i >= 4 && out[5 + i - 4] == '.' &&
        out[5 + i - 3] == 'E' && out[5 + i - 2] == 'L' && out[5 + i - 1] == 'F') {
        out[5 + i] = '\0';
        return;
    }
    out[5 + i] = '.'; out[6 + i] = 'E'; out[7 + i] = 'L'; out[8 + i] = 'F';
    out[9 + i] = '\0';
}

static void execute_line(char* line, char** envp) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return;
    if (*line == '#') return;

    char* args[16];
    int ac = 0;
    char* p = line;
    while (*p && ac < 15) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        args[ac++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    args[ac] = NULL;
    if (ac == 0) return;

    if (strcmp_(args[0], "exit") == 0) {
        sys_exit(0);
    }
    if (strcmp_(args[0], "cd") == 0) {
        if (ac > 1) {
            if (sys_chdir(args[1]) != 0) print("cd: failed\n");
        }
        return;
    }
    if (strcmp_(args[0], "echo") == 0) {
        for (int i = 1; i < ac; i++) {
            print(args[i]);
            if (i < ac - 1) print(" ");
        }
        print("\n");
        return;
    }
    if (strcmp_(args[0], "pwd") == 0) {
        char cwd[256];
        if (sys_getcwd(cwd, sizeof(cwd)) > 0) { print(cwd); print("\n"); }
        return;
    }

    char exec_path[256];
    resolve_cmd(args[0], exec_path);

    int pid = sys_fork();
    if (pid == 0) {
        int rc = sys_exec(exec_path, args, envp);
        print("sh: exec failed (");
        print_int(rc);
        print("): ");
        print(args[0]);
        print("\n");
        sys_exit(127);
    } else if (pid > 0) {
        int status = 0;
        sys_wait(pid, &status);
    } else {
        print("sh: fork failed\n");
    }
}

static void run_script(const char* path, char** envp) {
    int fd = sys_open(path, 0);
    if (fd < 0) {
        print("sh: cannot open ");
        print(path);
        print("\n");
        return;
    }

    char line[256];
    int pos = 0;
    char c;
    while (sys_read(fd, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            line[pos] = '\0';
            execute_line(line, envp);
            pos = 0;
        } else if (pos < 255) {
            line[pos++] = c;
        }
    }
    if (pos > 0) {
        line[pos] = '\0';
        execute_line(line, envp);
    }
    sys_close(fd);
}

int main(int argc, char** argv, char** envp) {
    if (argc > 1) {
        run_script(argv[1], envp);
        sys_exit(0);
    }

    char line[256];
    for (;;) {
        print("$ ");
        int n = sys_read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = '\0';
        
        if (n > 0 && line[n-1] == '\n') line[n-1] = '\0';
        execute_line(line, envp);
    }
}