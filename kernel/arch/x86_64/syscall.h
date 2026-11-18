#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

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

#define SYS_GETPID   39
#define SYS_EXIT     60
#define SYS_FB_TEXT  200
#define SYS_FB_RECT  201
#define SYS_POINTER  202
#define SYS_INPUT_POLL 203
#define SYS_FB_MMAP  204
#define SYS_MMAP_ANON 205
#define SYS_TTY_FOCUS 206
#define SYS_TTY_READ 207
#define SYS_TTY_WRITE 208
#define SYS_TTY_RESET 209
#define SYS_SERIAL_DEBUG 211
#define SYS_YIELD    210
#define SYS_FORK     230
#define SYS_EXEC     231
#define SYS_WAIT     232
#define SYS_SETNICE  240
#define SYS_GETNICE  241

#define SYS_REG_GET_U32 250
#define SYS_REG_GET_STR 251
#define SYS_REG_SET_U32 252
#define SYS_REG_SET_STR 253
#define SYS_REG_ENUM 254
#define SYS_TICKS 255
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
} NetInfoRequest;

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
} FramebufferMapping;

typedef struct {
    int32_t key;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t pointer_buttons;
} UserInputState;

typedef struct {
    uint64_t rax;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rip;
    uint64_t rflags;
    uint64_t rsp;
} SyscallContext;

typedef struct {
    char     name[64];
    uint8_t  attr;
    uint32_t size;
    uint32_t cluster;
} UserDirEntry;

void syscall_init(void);

uint64_t syscall_handler(SyscallContext* ctx);

void syscall_set_kstack(uint64_t kstack_top);

int copy_from_user(void* dst, const void* user_src, uint64_t size);
int copy_to_user(void* user_dst, const void* src, uint64_t size);

#endif