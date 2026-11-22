#ifndef USER_SYS_H
#define USER_SYS_H

#include <stdint.h>
#include <stddef.h>

#define SYS_CON_WRITE  1
#define SYS_READ    10
#define SYS_WRITE   11
#define SYS_OPEN    12
#define SYS_CLOSE   13
#define SYS_UNLINK  14
#define SYS_GETCWD  17
#define SYS_LSEEK   18
#define SYS_CHDIR   19
#define SYS_MKDIR   20
#define SYS_RMDIR   21
#define SYS_READDIR 22
#define SYS_OPENDIR 23
#define SYS_RMDIR_R 24

#define SYS_GETPID  39
#define SYS_EXIT   60
#define SYS_FB_TEXT 200
#define SYS_FB_RECT 201
#define SYS_POINTER 202
#define SYS_YIELD  210
#define SYS_FB_MMAP 204
#define SYS_INPUT_POLL 203
#define SYS_MMAP_ANON 205
#define SYS_TTY_FOCUS 206
#define SYS_TTY_READ 207
#define SYS_TTY_WRITE 208
#define SYS_TTY_RESET 209
#define SYS_SERIAL_DEBUG 211
#define SYS_FORK  230
#define SYS_EXEC  231
#define SYS_WAIT  232
#define SYS_SETNICE  240
#define SYS_GETNICE  241
#define SYS_REG_GET_U32 250
#define SYS_REG_GET_STR 251
#define SYS_REG_SET_U32 252
#define SYS_REG_SET_STR 253
#define SYS_REG_ENUM 254
#define SYS_TICKS 255
#define SYS_SPIN 256
#define SYS_WINDOW_POST 260
#define SYS_WINDOW_RECV 261
#define SYS_WINDOW_REPLY 262
#define SYS_WINDOW_REGISTER 263
#define SYS_NET_INFO 264
#define SYS_NET_POLL 265
#define SYS_NET_PING 266
#define SYS_NET_UDP_SEND 267
#define SYS_NET_UDP_RECV 268
#define SYS_NET_TCP_CONNECT 269
#define SYS_NET_TCP_SEND 270
#define SYS_NET_TCP_RECV 271
#define SYS_NET_TCP_CLOSE 272

#define NET_UDP_DATA_MAX 1400

typedef struct {
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t mac[6];
    uint16_t mtu;
} NetInfo;

typedef struct {
    uint32_t address;
    uint32_t timeout_ticks;
    uint32_t elapsed_ticks;
} NetPingRequest;

typedef struct {
    uint32_t address;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t length;
    uint8_t data[NET_UDP_DATA_MAX];
} NetUdpSendRequest;

typedef struct {
    uint16_t local_port;
    uint16_t source_port;
    uint32_t source_address;
    uint32_t capacity;
    uint32_t length;
    uint8_t data[NET_UDP_DATA_MAX];
} NetUdpReceiveRequest;

typedef struct {
    uint32_t address;
    uint16_t port;
    uint16_t reserved;
    uint32_t timeout_ticks;
} NetTcpConnectRequest;

typedef struct {
    uint32_t length;
    uint32_t timeout_ticks;
    uint8_t data[NET_UDP_DATA_MAX];
} NetTcpDataRequest;

#define FS_READ 0x01
#define FS_WRITE 0x02
#define FS_CREATE 0x04
#define FS_APPEND 0x08
#define FS_TRUNCATE 0x20

#define FS_ATTR_READONLY 0x01
#define FS_ATTR_HIDDEN   0x02
#define FS_ATTR_SYSTEM   0x04
#define FS_ATTR_DIR      0x10
#define FS_ATTR_FILE     0x20

typedef struct {
    char     name[64];
    uint8_t  attr;
    uint8_t  _pad[3];
    uint32_t size;
} DirEntry;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t fg;
    uint32_t bg;
    char text[128];
} FramebufferTextRequest;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
} FramebufferRectRequest;

typedef struct {
    uint64_t pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    uint32_t bpp;
} UserFramebuffer;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t buttons;
} PointerState;

typedef struct {
    int32_t key;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t pointer_buttons;
} UserInputState;

typedef struct {
    char name[32];
    char text[64];
    uint32_t type;
    uint32_t is_key;
} UserRegEntry;

#define USER_WINDOW_PAYLOAD 256
#define USER_WINDOW_MSG_NONE 0
#define USER_WINDOW_MSG_CREATE 1
#define USER_WINDOW_MSG_MOVE 2
#define USER_WINDOW_MSG_DESTROY 3
#define USER_WINDOW_MSG_SET_TITLE 4
#define USER_WINDOW_MSG_GROUP 5
#define USER_WINDOW_MSG_IMAGE 6
#define USER_WINDOW_MSG_ANIMATE 7
#define USER_WINDOW_MSG_REDRAW 8
#define USER_WINDOW_MSG_CREATE_CONTROL 9
#define USER_WINDOW_MSG_UPDATE_CONTROL 10
#define USER_WINDOW_MSG_INPUT 11
#define USER_WINDOW_MSG_CLOSE 12

#define USER_WINDOW_INPUT_POINTER 1
#define USER_WINDOW_INPUT_KEY 2
#define USER_WINDOW_INPUT_CLOSE 3

typedef struct {
    uint32_t type;
    int32_t window_id;
    int32_t owner_pid;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t control_type;
    int32_t control_id;
    int32_t selected;
    int32_t input_type;
    int32_t input_key;
    int32_t result;
    uint32_t payload_size;
    uint8_t payload[USER_WINDOW_PAYLOAD];
} UserWindowMessage;

static inline void* memset(void* p, int v, uint32_t n) {
    uint8_t* b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)v;
    return p;
}

static inline void* memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

extern void* malloc(size_t size);
extern void  free(void* ptr);
extern void* calloc(size_t n, size_t size);
extern void* realloc(void* ptr, size_t size);

static inline int sys_window_post(const UserWindowMessage* message);
static inline int sys_window_recv(UserWindowMessage* message);
static inline int sys_getpid(void);
static inline void sys_yield(void);

static inline void user_window_set_text(UserWindowMessage* message, const char* text) {
    if (!message || !text) return;
    uint32_t i = 0;
    message->payload_size = 0;
    while (text[i] && i + 1 < sizeof(message->payload)) {
        message->payload[i] = (uint8_t)text[i];
        i++;
    }
    message->payload_size = i;
    message->payload[i] = '\0';
}

static inline int sys_window_request_sync(UserWindowMessage* request, UserWindowMessage* response) {
    if (!request || !response || sys_window_post(request) != 0) return -1;
    for (uint32_t i = 0; i < 200000; i++) {
        if (sys_window_recv(response) == 0) {
            if (response->type == request->type && response->result == 0 &&
                (request->type != USER_WINDOW_MSG_CREATE || response->window_id >= 0)) {
                return 0;
            }
        }
        sys_yield();
    }
    return -1;
}

static inline int sys_window_create(const char* title, int x, int y, int width, int height) {
    UserWindowMessage request = {0};
    UserWindowMessage response = {0};
    request.type = USER_WINDOW_MSG_CREATE;
    request.owner_pid = (int32_t)sys_getpid();
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    user_window_set_text(&request, title);
    if (sys_window_request_sync(&request, &response) != 0) return -1;
    return (int)response.window_id;
}

static inline int sys_window_send_control(uint32_t type, int window_id, int control_id,
                                          int control_type, int x, int y,
                                          int width, int height, int selected,
                                          const char* text) {
    UserWindowMessage message = {0};
    message.type = type;
    message.window_id = window_id;
    message.control_id = control_id;
    message.control_type = control_type;
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.selected = selected;
    message.owner_pid = (int32_t)sys_getpid();
    user_window_set_text(&message, text ? text : "");
    return sys_window_post(&message);
}

static inline int sys_window_send_control_sync(uint32_t type, int window_id, int control_id,
                                                int control_type, int x, int y,
                                                int width, int height, int selected,
                                                const char* text) {
    UserWindowMessage request = {0};
    UserWindowMessage response = {0};
    request.type = type;
    request.window_id = window_id;
    request.control_id = control_id;
    request.control_type = control_type;
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    request.selected = selected;
    request.owner_pid = (int32_t)sys_getpid();
    user_window_set_text(&request, text ? text : "");
    if (sys_window_post(&request) != 0) return -1;
    for (uint32_t i = 0; i < 200000; i++) {
        if (sys_window_recv(&response) == 0) {
            if (response.type == type && response.window_id == window_id &&
                response.control_id == control_id && response.result == 0) {
                return response.control_id;
            }
        }
        sys_yield();
    }
    return -1;
}

static inline uint64_t syscall0(uint64_t n) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t syscall1(uint64_t n, uint64_t a0) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a0) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t syscall2(uint64_t n, uint64_t a0, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t syscall3(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int sys_con_write(const char* s, uint64_t len) {
    return (int)syscall2(SYS_CON_WRITE, (uint64_t)s, len);
}
static inline int sys_fb_text(int x, int y, uint32_t fg, uint32_t bg, const char* text) {
    FramebufferTextRequest request = { .x = x, .y = y, .fg = fg, .bg = bg };
    uint32_t i = 0;
    while (text && text[i] && i + 1 < sizeof(request.text)) {
        request.text[i] = text[i];
        i++;
    }
    request.text[i] = '\0';
    return (int)syscall1(SYS_FB_TEXT, (uint64_t)&request);
}
static inline int sys_fb_rect(int x, int y, uint32_t width, uint32_t height, uint32_t color) {
    FramebufferRectRequest request = {
        .x = x, .y = y, .width = width, .height = height, .color = color
    };
    return (int)syscall1(SYS_FB_RECT, (uint64_t)&request);
}
static inline int sys_fb_map(UserFramebuffer* framebuffer) {
    return (int)syscall1(SYS_FB_MMAP, (uint64_t)framebuffer);
}
static inline void* sys_mmap_anon(uint64_t size) {
    uint64_t address = syscall1(SYS_MMAP_ANON, size);
    return address == (uint64_t)-1 ? 0 : (void*)(uintptr_t)address;
}
static inline int sys_pointer(PointerState* state) {
    return (int)syscall1(SYS_POINTER, (uint64_t)state);
}
static inline int sys_input_poll(UserInputState* state) {
    return (int)syscall1(SYS_INPUT_POLL, (uint64_t)state);
}
static inline uint32_t sys_ticks(void) {
    return (uint32_t)syscall0(SYS_TICKS);
}
static inline int sys_net_info(NetInfo* info) {
    return (int)syscall1(SYS_NET_INFO, (uint64_t)info);
}
static inline void sys_net_poll(void) {
    syscall0(SYS_NET_POLL);
}
static inline int sys_net_ping(NetPingRequest* request) {
    return (int)syscall1(SYS_NET_PING, (uint64_t)request);
}
static inline int sys_net_udp_send(NetUdpSendRequest* request) {
    return (int)syscall1(SYS_NET_UDP_SEND, (uint64_t)request);
}
static inline int sys_net_udp_receive(NetUdpReceiveRequest* request) {
    return (int)syscall1(SYS_NET_UDP_RECV, (uint64_t)request);
}
static inline int sys_net_tcp_connect(NetTcpConnectRequest* request) {
    return (int)syscall1(SYS_NET_TCP_CONNECT, (uint64_t)request);
}
static inline int sys_net_tcp_send(NetTcpDataRequest* request) {
    return (int)syscall1(SYS_NET_TCP_SEND, (uint64_t)request);
}
static inline int sys_net_tcp_receive(NetTcpDataRequest* request) {
    return (int)syscall1(SYS_NET_TCP_RECV, (uint64_t)request);
}
static inline void sys_net_tcp_close(void) {
    syscall0(SYS_NET_TCP_CLOSE);
}
static inline int sys_tty_focus(int enabled) {
    return (int)syscall1(SYS_TTY_FOCUS, (uint64_t)enabled);
}
static inline int sys_tty_read(char* buffer, uint32_t capacity) {
    return (int)syscall2(SYS_TTY_READ, (uint64_t)buffer, capacity);
}
static inline int sys_tty_write(const char* buffer, uint32_t length) {
    return (int)syscall2(SYS_TTY_WRITE, (uint64_t)buffer, length);
}
static inline void sys_tty_reset(void) {
    syscall0(SYS_TTY_RESET);
}
static inline int sys_serial_debug(int enabled) {
    return (int)syscall1(SYS_SERIAL_DEBUG, (uint64_t)enabled);
}
static inline int sys_read(int fd, void* buf, uint64_t count) {
    return (int)syscall3(SYS_READ, fd, (uint64_t)buf, count);
}
static inline int sys_write(int fd, const void* buf, uint64_t count) {
    return (int)syscall3(SYS_WRITE, fd, (uint64_t)buf, count);
}
static inline int sys_open(const char* path, int flags) {
    return (int)syscall2(SYS_OPEN, (uint64_t)path, flags);
}
static inline int sys_reg_get_str(const char* path, char* out, uint32_t size) {
    return (int)syscall3(SYS_REG_GET_STR, (uint64_t)path, (uint64_t)out, size);
}
static inline int sys_reg_set_u32(const char* path, uint32_t value) {
    return (int)syscall2(SYS_REG_SET_U32, (uint64_t)path, value);
}
static inline int sys_reg_set_str(const char* path, const char* value) {
    return (int)syscall2(SYS_REG_SET_STR, (uint64_t)path, (uint64_t)value);
}
static inline int sys_reg_enum(const char* path, uint32_t index, UserRegEntry* out) {
    return (int)syscall3(SYS_REG_ENUM, (uint64_t)path, index, (uint64_t)out);
}
static inline int sys_close(int fd) {
    return (int)syscall1(SYS_CLOSE, fd);
}
static inline int sys_lseek(int fd, int64_t offset, int whence) {
    return (int)syscall3(SYS_LSEEK, fd, (uint64_t)offset, whence);
}
static inline int sys_mkdir(const char* path) {
    return (int)syscall1(SYS_MKDIR, (uint64_t)path);
}
static inline int sys_rmdir(const char* path) {
    return (int)syscall1(SYS_RMDIR, (uint64_t)path);
}
static inline int sys_getcwd(char* buf, uint64_t size) {
    return (int)syscall2(SYS_GETCWD, (uint64_t)buf, size);
}
static inline int sys_chdir(const char* path) {
    return (int)syscall1(SYS_CHDIR, (uint64_t)path);
}
static inline int sys_opendir(const char* path) {
    return (int)syscall1(SYS_OPENDIR, (uint64_t)path);
}
static inline int sys_readdir(int fd, uint32_t index, DirEntry* out) {
    return (int)syscall3(SYS_READDIR, fd, index, (uint64_t)out);
}
static inline int sys_unlink(const char* path) {
    return (int)syscall1(SYS_UNLINK, (uint64_t)path);
}
static inline int sys_rmdir_r(const char* path) {
    return (int)syscall1(SYS_RMDIR_R, (uint64_t)path);
}
static inline int sys_getpid(void) {
    return (int)syscall0(SYS_GETPID);
}
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    __builtin_unreachable();
}
static inline int sys_setnice(int nice) {
    return (int)syscall1(SYS_SETNICE, (uint64_t)nice);
}
static inline int sys_getnice(void) {
    return (int)syscall0(SYS_GETNICE);
}
static inline uint32_t sys_reg_get_u32(const char* path) {
    return (uint32_t)syscall1(SYS_REG_GET_U32, (uint64_t)path);
}
static inline int sys_exec(const char* path, char** argv, char** envp) {
    return (int)syscall3(SYS_EXEC, (uint64_t)path, (uint64_t)argv, (uint64_t)envp);
}
static inline int sys_fork(void) {
    return (int)syscall0(SYS_FORK);
}
static inline int sys_wait(int pid, int* status) {
    return (int)syscall2(SYS_WAIT, (uint64_t)pid, (uint64_t)status);
}
static inline int sys_window_post(const UserWindowMessage* message) {
    return (int)syscall1(SYS_WINDOW_POST, (uint64_t)message);
}
static inline int sys_window_recv(UserWindowMessage* message) {
    return (int)syscall1(SYS_WINDOW_RECV, (uint64_t)message);
}
static inline int sys_window_reply(int client_pid, const UserWindowMessage* message) {
    return (int)syscall2(SYS_WINDOW_REPLY, (uint64_t)client_pid, (uint64_t)message);
}
static inline uint64_t sys_spin(void) {
    return syscall0(SYS_SPIN);
}
static inline int sys_window_register(void) {
    return (int)syscall0(SYS_WINDOW_REGISTER);
}
static inline void sys_yield(void) {
    syscall0(SYS_YIELD);
}

static inline int strlen_(const char* s) {
    if (s == NULL) return -1;
    int n = 0;
    while (s[n]) n++;
    return n;
}
static inline int strcmp_(const char* a, const char* b) {
    if (a == NULL || b == NULL) return -1;
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline void strcpy_(char* d, const char* s) {
    if (d == NULL || s == NULL) return;
    while ((*d++ = *s++)) {}
}

static inline void print(const char* s) { sys_write(1, s, strlen_(s)); }
static inline void print_int(int value) {
    char digits[12];
    int count = 0;
    unsigned int number;
    if (value < 0) {
        print("-");
        number = (unsigned int)(-(value + 1)) + 1;
    } else {
        number = (unsigned int)value;
    }
    do {
        digits[count++] = '0' + number % 10;
        number /= 10;
    } while (number);
    while (count) {
        char ch[2] = { digits[--count], '\0' };
        print(ch);
    }
}


#endif