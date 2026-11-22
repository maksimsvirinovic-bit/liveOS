#include "syscall.h"
#include "pit/pit.h"
#include "fat32.h"
#include "heap.h"
#include "msr.h"
#include "path.h"
#include "percpu.h"
#include "ps2/ps2.h"
#include "regfs.h"
#include "graphics/framebuffer.h"
#include "net/net.h"
#include <stdio.h>
#include <vmm.h>
#include <x86_64/gdt/gdt.h>
#include <x86_64/gdt/tss.h>
#include <paging.h>
#include <pmm.h>
#include <serial/serial.h>
#include <tasking/process.h>
#include <tasking/scheduler.h>
#include <mutex.h>
#include <stddef.h>
#include <str.h>

#define SYSCALL_IO_MAX 4096
#define WINDOW_IPC_MAX 256
#define WINDOW_IPC_PAYLOAD 256

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
    uint8_t payload[WINDOW_IPC_PAYLOAD];
} WindowIpcMessage;

typedef struct {
    int sender_pid;
    int recipient_pid;
    WindowIpcMessage message;
} WindowIpcSlot;

static WindowIpcSlot window_ipc_queue[WINDOW_IPC_MAX];
static uint32_t window_ipc_head;
static uint32_t window_ipc_count;
static int window_server_pid = -1;
static mutex_t g_window_ipc_mutex = MUTEX_INIT;

static void window_ipc_cleanup_process(int pid) {
    if (pid <= 0) return;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < window_ipc_count; i++) {
        uint32_t index = (window_ipc_head + i) % WINDOW_IPC_MAX;
        WindowIpcSlot* slot = &window_ipc_queue[index];
        if (slot->sender_pid == pid || slot->recipient_pid == pid) continue;
        window_ipc_queue[kept++] = *slot;
    }
    for (uint32_t i = kept; i < WINDOW_IPC_MAX; i++) {
        window_ipc_queue[i].sender_pid = 0;
        window_ipc_queue[i].recipient_pid = 0;
        window_ipc_queue[i].message.type = 0;
        window_ipc_queue[i].message.window_id = 0;
        window_ipc_queue[i].message.owner_pid = 0;
        window_ipc_queue[i].message.payload_size = 0;
    }
    window_ipc_count = kept;
    window_ipc_head = 0;
}

extern int tty_input_read(char* buffer, uint32_t capacity);
extern void tty_output_write(const char* buffer, uint32_t length);
extern uint32_t tty_output_read(char* buffer, uint32_t capacity);
extern void tty_clear_input(void);
extern void tty_reset(void);

static int validate_user_range(Process* process, uint64_t address, uint64_t size, int write) {
    if (!process || !process->as || !address || address >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - address) return -1;
    for (uint64_t offset = 0; offset < size;) {
        uint64_t current = address + offset;
        if (!paging_user_access(process->as->pml4_phys, current, write)) return -1;
        uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));
        if (chunk > size - offset) chunk = size - offset;
        offset += chunk;
    }
    return 0;
}

extern void syscall_entry(void);
extern void vmm_yield_asm(AddressSpace* as, uint64_t rip, uint64_t rsp, uint64_t rflags);

static const int nice_to_weight[40] = {
    88761, 71755, 56483, 46273, 36291,
    29154, 23254, 18705, 14949, 11916,
    9548, 7620, 6100, 4904, 3906,
    3121, 2501, 1991, 1586, 1277,
    1024,
    820, 655, 526, 423,
    335, 272, 215, 172, 137,
    110, 87, 70, 56, 45,
    36, 29, 23, 18, 15,
};

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    uint64_t star = 0x00100008ULL << 32;
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);

    wrmsr(MSR_FMASK, 0x47700);

    serial_emergency_print("syscall: init STAR=");
    serial_emergency_print_hex(star);
    serial_emergency_print(" LSTAR=");
    serial_emergency_print_hex((uint64_t)(uintptr_t)syscall_entry);
    serial_emergency_print("\n");
}

void syscall_set_kstack(uint64_t kstack_top) {
    percpu_get(0)->kstack_top = kstack_top;
}

int copy_from_user(void* dst, const void* user_src, uint64_t size) {
    Process* process = process_get_current();
    uint64_t start = (uint64_t)(uintptr_t)user_src;
    if (!dst || !process || !process->as || !user_src || start >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - start) return -1;
    for (uint64_t offset = 0; offset < size;) {
        uint64_t address = start + offset;
        if (!paging_user_access(process->as->pml4_phys, address, 0)) return -1;
        uint64_t chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        if (chunk > size - offset) chunk = size - offset;
        memcpy((uint8_t*)dst + offset, (const void*)(uintptr_t)address, chunk);
        offset += chunk;
    }
    return 0;
}

int copy_to_user(void* user_dst, const void* src, uint64_t size) {
    Process* process = process_get_current();
    uint64_t start = (uint64_t)(uintptr_t)user_dst;
    if (!src || !process || !process->as || !user_dst || start >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - start) return -1;
    for (uint64_t offset = 0; offset < size;) {
        uint64_t address = start + offset;
        if (!paging_user_access(process->as->pml4_phys, address, 1)) return -1;
        uint64_t chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        if (chunk > size - offset) chunk = size - offset;
        memcpy((void*)(uintptr_t)address, (const uint8_t*)src + offset, chunk);
        offset += chunk;
    }
    return 0;
}

static int copy_user_path(Process* process, const char* user_path, char out[256]) {
    if (!process || copy_from_user_string(out, user_path, 256) != 0) return -1;
    char normalized[256];
    if (path_normalize(process->cwd_path, out, normalized, sizeof(normalized)) != 0) return -1;
    strcpy(out, normalized);
    return 0;
}


uint64_t syscall_handler(SyscallContext* ctx) {
    uint64_t num = ctx ? ctx->rax : 0;
    Process* current = process_get_current();
    if (current && current->uctx) {
        current->uctx->rax = ctx->rax;
        current->uctx->r15 = ctx->r15;
        current->uctx->r14 = ctx->r14;
        current->uctx->r13 = ctx->r13;
        current->uctx->r12 = ctx->r12;
        current->uctx->r10 = ctx->r10;
        current->uctx->r9  = ctx->r9;
        current->uctx->r8  = ctx->r8;
        current->uctx->rdi = ctx->rdi;
        current->uctx->rsi = ctx->rsi;
        current->uctx->rbp = ctx->rbp;
        current->uctx->rdx = ctx->rdx;
        current->uctx->rbx = ctx->rbx;
        current->uctx->rsp = ctx->rsp;
        current->uctx->rip = ctx->rip;
        current->uctx->rflags = ctx->rflags;
    }

    PerCpu* p = this_cpu();

    switch (num) {
        case SYS_CON_WRITE: {
            char buf[128];
            const char* user_buf = (const char*)ctx->rdi;
            uint64_t len = ctx->rsi;
            if (!user_buf || !len) return 0;
            uint64_t written = 0;
            while (written < len) {
                uint64_t chunk = len - written;
                if (chunk > sizeof(buf)) chunk = sizeof(buf);
                if (copy_from_user(buf, user_buf + written, chunk) != 0) return (uint64_t)-1;
                tty_output_write(buf, (uint32_t)chunk);
                if (serial_debug_enabled()) {
                    for (uint64_t i = 0; i < chunk; i++) serial_emergency_putc(buf[i]);
                }
                written += chunk;
            }
            return written;
        }
        case SYS_EXIT: {
            Process* exiting = process_get_current();
            int pid = exiting ? exiting->pid : -1;
            sys_exit_impl((int)ctx->rdi);
            window_ipc_cleanup_process(pid);
            for (;;) __asm__ volatile("cli; hlt");
        }
        case SYS_FB_TEXT: {
            FramebufferTextRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0) return (uint64_t)-1;
            request.text[sizeof(request.text) - 1] = '\0';
            Framebuffer* framebuffer = fb_get();
            if (!framebuffer || !framebuffer->pixels || request.x < 0 || request.y < 0 ||
                (uint32_t)request.x >= framebuffer->width || (uint32_t)request.y >= framebuffer->height) {
                return (uint64_t)-1;
            }
            size_t len = strlen(request.text);
            if (len > 32) len = 32;
            uint32_t max_chars_x = (framebuffer->width - (uint32_t)request.x) / 8;
            if (max_chars_x < len) len = max_chars_x;
            if ((uint32_t)request.y + 8 > framebuffer->height) return (uint64_t)-1;
            if (len == 0) return 0;
            char tmp[33];
            for (size_t i = 0; i < len; i++) tmp[i] = request.text[i];
            tmp[len] = '\0';
            fb_draw_string(request.x, request.y, tmp, request.fg, request.bg);
            return 0;
        }
        case SYS_FB_RECT: {
            FramebufferRectRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0) return (uint64_t)-1;
            Framebuffer* framebuffer = fb_get();
            if (!framebuffer || !framebuffer->pixels || request.width == 0 || request.height == 0 ||
                request.x < 0 || request.y < 0 || (uint32_t)request.x >= framebuffer->width ||
                (uint32_t)request.y >= framebuffer->height) return (uint64_t)-1;
            if ((uint32_t)request.x + request.width > framebuffer->width) {
                request.width = framebuffer->width - (uint32_t)request.x;
            }
            if ((uint32_t)request.y + request.height > framebuffer->height) {
                request.height = framebuffer->height - (uint32_t)request.y;
            }
            if (request.width == 0 || request.height == 0) return 0;
            fb_fill_rect(request.x, request.y, request.width, request.height, request.color);
            return 0;
        }
        case SYS_FB_MMAP: {
            Process* process = process_get_current();
            Framebuffer* framebuffer = fb_get();
            uint64_t user_info = ctx->rdi;
            if (!process || !process->as || !framebuffer || !framebuffer->pixels ||
                framebuffer->bpp != 32 || framebuffer->pitch_pixels == 0 ||
                validate_user_range(process, user_info, sizeof(FramebufferMapping), 1) != 0) {
                return (uint64_t)-1;
            }

            uint64_t mapped_size = framebuffer->map_size;
            uint64_t mapped_start = vma_find_free(&process->as->vmas, 0x20000000ULL,
                                                   0x0000800000000000ULL, mapped_size);
            if (!mapped_start) return (uint64_t)-1;

            uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_NOCACHE;
            uint64_t mapped_pages = 0;
            for (; mapped_pages < mapped_size; mapped_pages += PAGE_SIZE) {
                if (paging_map_4k(process->as->pml4_phys,
                                  framebuffer->phys_base + mapped_pages,
                                  mapped_start + mapped_pages, flags) != 0) break;
            }
            if (mapped_pages != mapped_size) {
                while (mapped_pages > 0) {
                    mapped_pages -= PAGE_SIZE;
                    paging_unmap_4k(process->as->pml4_phys, mapped_start + mapped_pages);
                }
                return (uint64_t)-1;
            }

            if (vma_add(&process->as->vmas, mapped_start, mapped_start + mapped_size,
                        flags, VMA_FRAMEBUFFER) != 0) {
                for (uint64_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
                    paging_unmap_4k(process->as->pml4_phys, mapped_start + offset);
                }
                return (uint64_t)-1;
            }

            FramebufferMapping mapping = {
                .pixels = mapped_start + framebuffer->offset,
                .width = framebuffer->width,
                .height = framebuffer->height,
                .pitch_pixels = framebuffer->pitch_pixels,
                .bpp = framebuffer->bpp,
            };
            ps2_set_pointer_bounds(framebuffer->width, framebuffer->height);
            return copy_to_user((void*)(uintptr_t)user_info, &mapping, sizeof(mapping)) == 0
                ? 0 : (uint64_t)-1;
        }
        case SYS_MMAP_ANON: {
            Process* process = process_get_current();
            uint64_t size = ctx->rdi;
            if (!process || !process->as || size == 0 ||
                size > 0x40000000ULL || size > 0x0000800000000000ULL - (PAGE_SIZE - 1)) {
                return (uint64_t)-1;
            }
            uint64_t mapped_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t mapped_start = vma_find_free(&process->as->vmas, 0x20000000ULL,
                                                   0x0000800000000000ULL, mapped_size);
            if (!mapped_start) return (uint64_t)-1;

            uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
            uint64_t mapped_pages = 0;
            for (; mapped_pages < mapped_size; mapped_pages += PAGE_SIZE) {
                uint64_t phys = pmm_alloc_page();
                if (!phys) break;
                if (paging_map_4k(process->as->pml4_phys, phys,
                                  mapped_start + mapped_pages, flags) != 0) {
                    pmm_free_page(phys);
                    break;
                }
            }
            if (mapped_pages != mapped_size) {
                while (mapped_pages > 0) {
                    mapped_pages -= PAGE_SIZE;
                    uint64_t phys = paging_get_phys(process->as->pml4_phys,
                                                    mapped_start + mapped_pages);
                    paging_unmap_4k(process->as->pml4_phys, mapped_start + mapped_pages);
                    if (phys) pmm_free_page(phys);
                }
                return (uint64_t)-1;
            }

            if (vma_add(&process->as->vmas, mapped_start, mapped_start + mapped_size,
                        flags, VMA_BACKBUFFER) != 0) {
                for (uint64_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
                    uint64_t phys = paging_get_phys(process->as->pml4_phys, mapped_start + offset);
                    paging_unmap_4k(process->as->pml4_phys, mapped_start + offset);
                    if (phys) pmm_free_page(phys);
                }
                return (uint64_t)-1;
            }
            return mapped_start;
        }
        case SYS_POINTER: {
            PointerState state;
            ps2_get_pointer_state(&state);
            return copy_to_user((void*)(uintptr_t)ctx->rdi, &state, sizeof(state)) == 0 ? 0 : (uint64_t)-1;
        }
        case SYS_INPUT_POLL: {
            UserInputState state;
            PointerState pointer;
            state.key = kbd_getchar();
            ps2_get_pointer_state(&pointer);
            state.pointer_x = pointer.x;
            state.pointer_y = pointer.y;
            state.pointer_buttons = pointer.buttons;
            return copy_to_user((void*)(uintptr_t)ctx->rdi, &state, sizeof(state)) == 0 ? 0 : (uint64_t)-1;
        }
        case SYS_NET_INFO: {
            NetInfo info;
            if (net_get_info(&info) != 0) return (uint64_t)-1;
            return copy_to_user((void*)(uintptr_t)ctx->rdi, &info, sizeof(info)) == 0
                ? 0 : (uint64_t)-1;
        }
        case SYS_NET_POLL:
            net_poll();
            return 0;
        case SYS_NET_PING: {
            NetPingRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0) {
                return (uint64_t)-1;
            }
            if (net_ping(request.address, request.timeout_ticks, &request.elapsed_ticks) != 0) {
                return (uint64_t)-1;
            }
            return copy_to_user((void*)(uintptr_t)ctx->rdi, &request, sizeof(request)) == 0
                ? 0 : (uint64_t)-1;
        }
        case SYS_NET_UDP_SEND: {
            NetUdpSendRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0 ||
                request.length > NET_UDP_DATA_MAX) return (uint64_t)-1;
            return net_udp_send(request.address, request.source_port,
                                request.destination_port, request.data, request.length) == 0
                ? 0 : (uint64_t)-1;
        }
        case SYS_NET_UDP_RECV: {
            NetUdpReceiveRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0 ||
                request.capacity > NET_UDP_DATA_MAX) return (uint64_t)-1;
            int result = net_udp_receive(request.local_port, request.data, request.capacity,
                                         &request.length, &request.source_address,
                                         &request.source_port);
            if (result <= 0) return (uint64_t)result;
            return copy_to_user((void*)(uintptr_t)ctx->rdi, &request, sizeof(request)) == 0
                ? 1 : (uint64_t)-1;
        }
        case SYS_NET_TCP_CONNECT: {
            NetTcpConnectRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0) {
                return (uint64_t)-1;
            }
            return net_tcp_connect(request.address, request.port, request.timeout_ticks) == 0
                ? 0 : (uint64_t)-1;
        }
        case SYS_NET_TCP_SEND: {
            NetTcpDataRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0 ||
                request.length > NET_UDP_DATA_MAX) return (uint64_t)-1;
            return (uint64_t)net_tcp_send(request.data, request.length, request.timeout_ticks);
        }
        case SYS_NET_TCP_RECV: {
            NetTcpDataRequest request;
            if (copy_from_user(&request, (const void*)ctx->rdi, sizeof(request)) != 0 ||
                request.length > NET_UDP_DATA_MAX) return (uint64_t)-1;
            uint32_t received = 0;
            int result = net_tcp_receive(request.data, request.length, &received,
                                         request.timeout_ticks);
            if (result <= 0) return (uint64_t)result;
            request.length = received;
            if (copy_to_user((void*)(uintptr_t)ctx->rdi, &request, sizeof(request)) != 0) {
                return (uint64_t)-1;
            }
            return received;
        }
        case SYS_NET_TCP_CLOSE:
            net_tcp_close();
            return 0;
        case SYS_WINDOW_POST: {
            Process* sender = process_get_current();
            WindowIpcMessage message;
            if (!sender || sender->pid == window_server_pid ||
                copy_from_user(&message, (const void*)ctx->rdi, sizeof(message)) != 0 ||
                message.payload_size > WINDOW_IPC_PAYLOAD) return (uint64_t)-1;

            if (message.payload_size < WINDOW_IPC_PAYLOAD) {
                memset(message.payload + message.payload_size, 0,
                    WINDOW_IPC_PAYLOAD - message.payload_size);
            }

            mutex_lock(&g_window_ipc_mutex);
            if (window_server_pid < 0) {
                mutex_unlock(&g_window_ipc_mutex);
                return (uint64_t)-1;
            }
            if (window_ipc_count >= WINDOW_IPC_MAX) {
                window_ipc_head = (window_ipc_head + 1) % WINDOW_IPC_MAX;
                window_ipc_count--;
            }
            uint32_t slot = (window_ipc_head + window_ipc_count) % WINDOW_IPC_MAX;
            window_ipc_queue[slot].sender_pid = sender->pid;
            window_ipc_queue[slot].recipient_pid = window_server_pid;
            message.owner_pid = sender->pid;
            window_ipc_queue[slot].message = message;
            window_ipc_count++;

            Process* server = process_get_by_pid(window_server_pid);
            if (server && server->state == PROC_SLEEPING) {
                scheduler_wakeup(server);
            }
            mutex_unlock(&g_window_ipc_mutex);
            return 0;
        }
        case SYS_WINDOW_RECV: {
            Process* receiver = process_get_current();
            if (!receiver) return (uint64_t)-1;
            for (;;) {
                mutex_lock(&g_window_ipc_mutex);
                for (uint32_t i = 0; i < window_ipc_count; i++) {
                    uint32_t index = (window_ipc_head + i) % WINDOW_IPC_MAX;
                    WindowIpcSlot* slot = &window_ipc_queue[index];
                    if (slot->recipient_pid != receiver->pid) continue;

                    WindowIpcMessage message = slot->message;
                    if (message.payload_size < WINDOW_IPC_PAYLOAD) {
                        memset(message.payload + message.payload_size, 0,
                            WINDOW_IPC_PAYLOAD - message.payload_size);
                    }
                    if (copy_to_user((void*)(uintptr_t)ctx->rdi, &message,
                                    sizeof(message)) != 0) {
                        mutex_unlock(&g_window_ipc_mutex);
                        return (uint64_t)-1;
                    }
                    for (uint32_t j = i; j + 1 < window_ipc_count; j++) {
                        uint32_t to = (window_ipc_head + j) % WINDOW_IPC_MAX;
                        uint32_t from = (window_ipc_head + j + 1) % WINDOW_IPC_MAX;
                        window_ipc_queue[to] = window_ipc_queue[from];
                    }
                    window_ipc_count--;
                    window_ipc_head = 0;
                    mutex_unlock(&g_window_ipc_mutex);
                    return 0;
                }
                if (receiver->pid == window_server_pid) {
                    mutex_unlock(&g_window_ipc_mutex);
                    return (uint64_t)-1;
                }
                mutex_unlock(&g_window_ipc_mutex);
                scheduler_sleep();
            }
        }
        case SYS_WINDOW_REPLY: {
            Process* sender = process_get_current();
            int client_pid = (int)ctx->rdi;
            WindowIpcMessage message;
            if (!sender || sender->pid != window_server_pid ||
                copy_from_user(&message, (const void*)ctx->rsi, sizeof(message)) != 0 ||
                message.payload_size > WINDOW_IPC_PAYLOAD) return (uint64_t)-1;
            if (message.payload_size < WINDOW_IPC_PAYLOAD) {
                memset(message.payload + message.payload_size, 0,
                    WINDOW_IPC_PAYLOAD - message.payload_size);
            }
            Process* client = process_get_by_pid(client_pid);
            if (!client || !client->as || window_ipc_count >= WINDOW_IPC_MAX) return (uint64_t)-1;
            if (client && client->state == PROC_SLEEPING) scheduler_wakeup(client);
            uint32_t slot = (window_ipc_head + window_ipc_count) % WINDOW_IPC_MAX;
            window_ipc_queue[slot].sender_pid = sender->pid;
            window_ipc_queue[slot].recipient_pid = client_pid;
            message.owner_pid = sender->pid;
            window_ipc_queue[slot].message = message;
            window_ipc_count++;
            return 0;
        }
        case SYS_WINDOW_REGISTER: {
            Process* server = process_get_current();
            if (!server || (window_server_pid >= 0 && window_server_pid != server->pid)) return (uint64_t)-1;
            window_server_pid = server->pid;
            return 0;
        }
        case SYS_TICKS:
            return pit_get_tick();
        case SYS_TTY_FOCUS: {
            kbd_set_tty_focus(ctx->rdi != 0);
            return 0;
        }
        case SYS_TTY_READ: {
            char buffer[128];
            uint32_t capacity = (uint32_t)ctx->rsi;
            if (!capacity || capacity > sizeof(buffer)) return (uint64_t)-1;
            uint32_t count = tty_output_read(buffer, capacity);
            if (!count) return 0;
            return copy_to_user((void*)(uintptr_t)ctx->rdi, buffer, (uint64_t)count) == 0
                ? (uint64_t)count : (uint64_t)-1;
        }
        case SYS_TTY_WRITE: {
            char buffer[128];
            uint32_t length = (uint32_t)ctx->rsi;
            if (!kbd_tty_focus() || !length || length > sizeof(buffer) ||
                copy_from_user(buffer, (const void*)ctx->rdi, length) != 0) return (uint64_t)-1;
            tty_output_write(buffer, length);
            return length;
        }
        case SYS_TTY_RESET: {
            tty_reset();
            return 0;
        }
        case SYS_SERIAL_DEBUG: {
            serial_set_debug_enabled(ctx->rdi != 0);
            return 0;
        }
        case SYS_YIELD: {
            __asm__ volatile("sti; hlt; cli" ::: "memory");
            return 0;
        }
        case SYS_WAIT: {
            int pid = (int)ctx->rdi;
            int* user_status = (int*)ctx->rsi;
            int local_status = 0;
            int ret = sys_wait_impl(pid, &local_status);
            if (user_status && ret > 0) {
                copy_to_user(user_status, &local_status, sizeof(int));
            }
            return (uint64_t)ret;
        }
        case SYS_FORK: {
            return (uint64_t)sys_fork_impl(ctx);
        }
        case SYS_EXEC: {
            const char* path = (const char*)ctx->rdi;
            char** argv = (char**)ctx->rsi;
            char** envp = (char**)ctx->rdx;
            return (uint64_t)sys_exec_impl(path, argv, envp, ctx);
        }
        case SYS_SETNICE: {
            int pid = (int)ctx->rdi;
            int nice = (int)ctx->rsi;
            if (nice < -20) nice = -20;
            if (nice > 19) nice = 19;

            Process* p = (pid == 0) ? process_get_current() : process_get_by_pid(pid);
            if (!p) return (uint64_t)-1;

            p->weight = nice_to_weight[nice + 20];
            return 0;
        }

        case SYS_GETNICE: {
            int pid = (int)ctx->rdi;
            Process* p = (pid == 0) ? process_get_current() : process_get_by_pid(pid);
            if (!p) return (uint64_t)-1;
            for (int i = 0; i < 40; i++) {
                if (nice_to_weight[i] == p->weight) return i - 20;
            }
            return 0;
        }
        case SYS_GETPID: {
            return (uint64_t)sys_getpid_impl();
        }
        case SYS_GETCWD: {
            Process* p = process_get_current();
            char* user_buf = (char*)ctx->rdi;
            uint64_t size = ctx->rsi;
            if (!p || !user_buf || size == 0) return (uint64_t)-1;

            uint64_t length = 0;
            while (length < sizeof(p->cwd_path) && p->cwd_path[length]) length++;
            if (length == sizeof(p->cwd_path) || size <= length) return (uint64_t)-1;
            if (copy_to_user(user_buf, p->cwd_path, length + 1) != 0) {
                return (uint64_t)-1;
            }
            return length + 1;
        }
        case SYS_OPENDIR: {
            Process* p = process_get_current();
            if (!p) return (uint64_t)-1;
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;

            File* dir = NULL;
            if (fs_opendir(kpath, &dir) != 0) return (uint64_t)-1;

            for (int i = 3; i < PROC_MAX_FD; i++) {
                if (!p->fds[i]) { p->fds[i] = dir; return (uint64_t)i; }
            }
            fs_closedir(dir);
            return (uint64_t)-1;
        }

        case SYS_READDIR: {
            int fd = (int)ctx->rdi;
            uint32_t index = (uint32_t)ctx->rsi;
            void* user_out = (void*)ctx->rdx;
            Process* p = process_get_current();
            if (!p || !user_out || fd < 3 || fd >= PROC_MAX_FD || !p->fds[fd]) return (uint64_t)-1;

            DirEntry e;
            if (fs_readdir(p->fds[fd], index, &e) != 0) return (uint64_t)-1;

            if (copy_to_user(user_out, &e, sizeof(e)) != 0) return (uint64_t)-1;
            return 0;
        }

        case SYS_UNLINK: {
            Process* p = process_get_current();
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;
            return (uint64_t)fs_unlink(kpath);
        }

        case SYS_RMDIR: {
            Process* p = process_get_current();
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;
            return (uint64_t)fs_rmdir(kpath);
        }

        case SYS_RMDIR_R: {
            Process* p = process_get_current();
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;
            return (uint64_t)fs_rmdir_r(kpath);
        }
        case SYS_CHDIR: {
            Process* p = process_get_current();
            if (!p) return (uint64_t)-1;
            const char* user_path = (const char*)ctx->rdi;
            if (!user_path) return (uint64_t)-1;
            char kpath[256];
            if (copy_from_user_string(kpath, user_path, sizeof(kpath)) != 0) return (uint64_t)-1;

            char norm[256];
            if (path_normalize(p->cwd_path, kpath, norm, sizeof(norm)) != 0) return (uint64_t)-1;

            FileSystem* fs = fs_find_by_path("/");
            if (!fs || !fs->private) return (uint64_t)-1;
            Fat32* f = (Fat32*)fs->private;

            PathResult pr;
            if (fat32_resolve(f, f->root_cluster, norm, &pr) != 0) return (uint64_t)-1;
            if (pr.is_file) return (uint64_t)-1;

            p->cwd_cluster = pr.cluster;
            strcpy(p->cwd_path, norm);
            return 0;
        }
        case SYS_OPEN: {
            uint32_t flags = (uint32_t)ctx->rsi;
            Process* p = process_get_current();
            if (!p) return (uint64_t)-1;
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;

            File* f = NULL;
            if (fs_open(kpath, flags, &f) != 0) return (uint64_t)-1;

            for (int i = 3; i < PROC_MAX_FD; i++) {
                if (!p->fds[i]) {
                    p->fds[i] = f;
                    return (uint64_t)i;
                }
            }
            fs_close(f);
            return (uint64_t)-1;
        }

        case SYS_READ: {
            int fd = (int)ctx->rdi;
            void* buf = (void*)ctx->rsi;
            uint64_t count = ctx->rdx;
            Process* p = process_get_current();
            if (!p || !buf || count == 0 || count > SYSCALL_IO_MAX ||
                validate_user_range(p, (uint64_t)(uintptr_t)buf, count, 1) != 0) return (uint64_t)-1;

            if (fd == 0) {
                uint8_t line[SYSCALL_IO_MAX];
                for (;;) {
                    while (!kbd_tty_focus()) __asm__ volatile("sti; hlt" ::: "memory");
                    int available = tty_input_read((char*)line, sizeof(line));
                    if (available > 0) {
                        if (copy_to_user(buf, line, (uint64_t)available) != 0) return (uint64_t)-1;
                        return (uint64_t)available;
                    }
                    __asm__ volatile("sti; hlt" ::: "memory");
                }
            }

            if (fd < 0 || fd >= PROC_MAX_FD || !p->fds[fd]) return (uint64_t)-1;

            uint8_t* tmp = (uint8_t*)kmalloc(count);
            if (!tmp) return (uint64_t)-1;
            int64_t n = fs_read(p->fds[fd], tmp, count);
            if (n > 0) {
                if (copy_to_user(buf, tmp, n) != 0) { kfree(tmp); return (uint64_t)-1; }
            }
            kfree(tmp);
            return (uint64_t)n;
        }

        case SYS_WRITE: {
            int fd = (int)ctx->rdi;
            const void* buf = (const void*)ctx->rsi;
            uint64_t count = ctx->rdx;
            Process* p = process_get_current();
            if (!p || !buf || count > SYSCALL_IO_MAX ||
                validate_user_range(p, (uint64_t)(uintptr_t)buf, count, 0) != 0) return (uint64_t)-1;

            if (fd == 1 || fd == 2) {
                char tmp[128];
                uint64_t written = 0;
                while (written < count) {
                    uint64_t chunk = count - written;
                    if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
                    if (copy_from_user(tmp, (const uint8_t*)buf + written, chunk) != 0) return (uint64_t)-1;
                    tty_output_write(tmp, (uint32_t)chunk);
                    if (serial_debug_enabled()) {
                        for (uint64_t i = 0; i < chunk; i++) serial_emergency_putc(tmp[i]);
                    }
                    written += chunk;
                }
                return count;
            }

            if (fd < 0 || fd >= PROC_MAX_FD || !p->fds[fd]) return (uint64_t)-1;

            uint8_t* tmp = (uint8_t*)kmalloc(count);
            if (!tmp) return (uint64_t)-1;
            if (copy_from_user(tmp, buf, count) != 0) { kfree(tmp); return (uint64_t)-1; }
            int64_t n = fs_write(p->fds[fd], tmp, count);
            kfree(tmp);
            return (uint64_t)n;
        }

        case SYS_CLOSE: {
            int fd = (int)ctx->rdi;
            Process* p = process_get_current();
            if (!p || fd < 3 || fd >= PROC_MAX_FD || !p->fds[fd]) return (uint64_t)-1;
            fs_close(p->fds[fd]);
            p->fds[fd] = NULL;
            return 0;
        }

        case SYS_LSEEK: {
            int fd = (int)ctx->rdi;
            int64_t offset = (int64_t)ctx->rsi;
            int whence = (int)ctx->rdx;
            Process* p = process_get_current();
            if (!p || fd < 0 || fd >= PROC_MAX_FD || !p->fds[fd]) return (uint64_t)-1;
            return (uint64_t)fs_seek(p->fds[fd], offset, whence);
        }

        case SYS_REG_GET_U32: {
            const char* path = (const char*)ctx->rdi;
            char kpath[256];
            if (copy_from_user_string(kpath, path, sizeof(kpath)) != 0) return (uint64_t)-1;
            FileSystem* regfs_fs = fs_find_by_path("/reg");
            if (!regfs_fs || !regfs_fs->private) return (uint64_t)-1;
            RegFS* rfs = (RegFS*)regfs_fs->private;
            return (uint64_t)regfs_get_u32(rfs, kpath);
        }

        case SYS_REG_GET_STR: {
            const char* path = (const char*)ctx->rdi;
            char* user_buf = (char*)ctx->rsi;
            uint32_t size = (uint32_t)ctx->rdx;
            char kpath[256];
            if (copy_from_user_string(kpath, path, sizeof(kpath)) != 0) return (uint64_t)-1;
            if (!user_buf || size == 0) return (uint64_t)-1;
            FileSystem* regfs_fs = fs_find_by_path("/reg");
            if (!regfs_fs || !regfs_fs->private) return (uint64_t)-1;
            RegFS* rfs = (RegFS*)regfs_fs->private;
            char tmp[256];
            if (regfs_get_str(rfs, kpath, tmp, sizeof(tmp)) < 0) return (uint64_t)-1;
            int len = strlen(tmp) + 1;
            if (len > (int)size) len = size;
            if (copy_to_user(user_buf, tmp, len) != 0) return (uint64_t)-1;
            return len;
        }

        case SYS_REG_SET_U32: {
            char path[256];
            if (copy_from_user_string(path, (const char*)ctx->rdi, sizeof(path)) != 0) return (uint64_t)-1;
            FileSystem* regfs_fs = fs_find_by_path("/reg");
            if (!regfs_fs || !regfs_fs->private) return (uint64_t)-1;
            return (uint64_t)regfs_set_u32((RegFS*)regfs_fs->private, path, (uint32_t)ctx->rsi);
        }

        case SYS_REG_SET_STR: {
            char path[256];
            char value[4096];
            if (copy_from_user_string(path, (const char*)ctx->rdi, sizeof(path)) != 0 ||
                copy_from_user_string(value, (const char*)ctx->rsi, sizeof(value)) != 0) {
                return (uint64_t)-1;
            }
            FileSystem* regfs_fs = fs_find_by_path("/reg");
            if (!regfs_fs || !regfs_fs->private) return (uint64_t)-1;
            return (uint64_t)regfs_set_str((RegFS*)regfs_fs->private, path, value);
        }

        case SYS_REG_ENUM: {
            char path[256];
            RegEntryInfo entry;
            if (copy_from_user_string(path, (const char*)ctx->rdi, sizeof(path)) != 0) return (uint64_t)-1;
            FileSystem* regfs_fs = fs_find_by_path("/reg");
            if (!regfs_fs || !regfs_fs->private ||
                regfs_enum((RegFS*)regfs_fs->private, path, (uint32_t)ctx->rsi, &entry) != 0) {
                return (uint64_t)-1;
            }
            return copy_to_user((void*)(uintptr_t)ctx->rdx, &entry, sizeof(entry)) == 0
                ? 0 : (uint64_t)-1;
        }

        case SYS_MKDIR: {
            Process* p = process_get_current();
            if (!p) return (uint64_t)-1;
            char kpath[256];
            if (copy_user_path(p, (const char*)ctx->rdi, kpath) != 0) return (uint64_t)-1;
            return (uint64_t)fs_mkdir(kpath);
        }
        case SYS_SPIN: {
            uint64_t tick = 0;
            __asm__ volatile("rdtsc" : "=A"(tick));
            return tick;
        }
        default:
            serial_emergency_print("[syscall] unknown: ");
            serial_emergency_print_hex(num);
            serial_emergency_print(" from cpu ");
            serial_emergency_print_hex(p->cpu_id);
            serial_emergency_print("\n");
            return (uint64_t)-1;
    }
}