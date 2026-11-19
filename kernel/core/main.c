#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <str.h>
#include <boot_info.h>
#include <x86_64/io.h>

#include "ops/driver.h"
#include "serial/serial.h"
#include "keyboard/keyboard.h"
#include "ps2/ps2.h"
#include "pit/pit.h"
#include "x86_64/interrupts/irq.h"
#include "x86_64/interrupts/exceptions.h"
#include "x86_64/gdt/gdt.h"
#include "x86_64/gdt/tss.h"
#include "x86_64/percpu.h"
#include "x86_64/syscall.h"
#include "x86_64/pci.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "vmalloc.h"
#include "vmm.h"
#include "pf.h"
#include "user_abi.h"
#include "init.h"
#include "fs.h"
#include "tmpfs.h"
#include "fat32.h"
#include "regfs.h"
#include "block_cache.h"
#include "block.h"
#include "symbol.h"
#include "framebuffer.h"
#include "tasking/scheduler.h"
#include "tasking/process.h"
#include "panic.h"
#include "config.h"
#include "path.h"
#include "net/netdev.h"
#include "drivers/e1000/e1000.h"
#include "net/net.h"

extern int nvme_register(void);

extern unsigned char fork_test_elf_data[];
extern unsigned int  fork_test_elf_data_len;
extern unsigned char read_test_elf_data[];
extern unsigned int  read_test_elf_data_len;

static void init_drivers(void) {
    block_init();
    driver_subsystem_init();
    serial_driver_register();
    keyboard_driver_register();
    driver_init_all();
    stdio_init();
    printf("[init] drivers\n");
}

static void init_cpu(void) {
    gdt_init();
    tss_init();
    exceptions_init();
    pf_init();
    irq_init();
    percpu_init_bsp();
    syscall_init();
    printf("[init] cpu\n");
}

static void init_memory(const BOOT_INFO* bi) {
    pmm_init(bi);
    paging_init(bi->KernelPml4Phys,
                bi->KernelPhysBase,
                bi->KernelPhysSize,
                pmm_total_pages() * PAGE_SIZE);
    kheap_init();
    vmalloc_init();
    vmm_init(bi->KernelPml4Phys);
    user_abi_init();
    printf("[init] memory\n");
}

static BlockDevice* rootfs_init(void) {
    BlockDevice* dev = block_find("nvme0");
    if (!dev) {
        printf("[init] rootfs: nvme0 not found\n");
        return NULL;
    }
    FileSystem* root_fs = NULL;
    if (fs_mount(dev, "fat32", "/", &root_fs) == 0) {
        printf("fat32: mounted at /\n");
    } else {
        panic("fat32 mount failed");
    }

    regfs_set_hive_path("/reg/SYSTEM.HIV");
    FileSystem* regfs_fs = NULL;
    if (fs_mount(dev, "regfs", "/reg", &regfs_fs) == 0) {
        printf("regfs: mounted at /reg\n");
        config_load();
        config_dump();
    } else {
        printf("regfs: mount failed. System loaded with defaults. All changes will be lost on shutdown!\n");
        config_defaults();
    }
    return dev;
}

static BlockDevice* init_fs(void) {
    bcache_init();
    fs_init();
    symbol_init();
    tmpfs_init();
    regfs_init();
    fat32_init();
    BlockDevice* dev = rootfs_init();
    printf("[init] fs\n");
    return dev;
}

static void init_graphics(const BOOT_INFO* bi) {
    if (fb_init(bi) != 0) {
        printf("[init] fb: not available\n");
        return;
    }
    fb_clear(COLOR_BLACK);
    fb_draw_string(8, 8, "LiveOS", COLOR_WHITE, COLOR_BLACK);
    printf("[init] framebuffer text console\n");
}

static void init_disks(void) {
    int n = nvme_register();
    if (n > 0) {
        printf("[init] nvme: %d disk(s)\n", n);
    } else {
        printf("[init] nvme: not available\n");
    }
}

static void init_network(void) {
    netdev_init();
    if (!g_config.e1000_enabled) {
        printf("[init] e1000 disabled by config\n");
        return;
    }
    if (e1000_init() == 0) {
        net_init();
        printf("[init] e1000 network interface ready\n");
    }
}

static void init_input(void) {
    if (g_config.ps2_enabled) {
        ps2_init();
        printf("[init] keyboard input\n");
    } else {
        printf("[init] input disabled by config\n");
    }
}

noreturn void kmain(BOOT_INFO* bi) {
    serial_emergency_init();
    serial_set_debug_enabled(1);
    if (!bi) { serial_force_print("kmain: no BOOT_INFO\n"); for(;;) __asm__ volatile("cli; hlt"); }
    serial_emergency_print("Kernel reached kmain (higher-half)\n");

    init_drivers();
    init_cpu();
    init_memory(bi);
    pci_init();
    init_disks();
    init_fs();
    init_network();
    init_graphics(bi);
    init_input();
    pit_init();

    printf("LiveOS: init complete\n");

    sti();

    process_init();
    scheduler_init();
    printf("[init] process + scheduler\n");

    printf("LiveOS: starting user init\n");

    if (!g_config.allow_user_mode) {
        printf("[init] user mode disabled by config\n");
        for (;;) __asm__ volatile("sti; hlt");
    }

    Process* usr = init_load_program(g_config.default_shell, 2, "shell");
    if (usr) {
        FileSystem* root_fs = fs_find_by_path("/");
        char home_path[256];
        PathResult home;
        if (root_fs && root_fs->private && g_config.default_home[0] &&
            path_normalize("/", g_config.default_home, home_path, sizeof(home_path)) == 0 &&
            fat32_resolve((Fat32*)root_fs->private,
                          ((Fat32*)root_fs->private)->root_cluster,
                          home_path, &home) == 0 && !home.is_file) {
            usr->cwd_cluster = home.cluster;
            strcpy(usr->cwd_path, home_path);
        } else {
            printf("[init] configured home unavailable, using /\n");
        }
        scheduler_add(usr);
        printf("[init] shell loaded from %s (pid=%d, cwd=%s)\n",
               g_config.default_shell, usr->pid, usr->cwd_path);
    } else {
        printf("[init] failed to load configured shell: %s\n", g_config.default_shell);
    }

    if (g_config.desktop_autostart) {
        Process* desktop = init_load_program("/bin/DESKTOP.ELF", 0, "desktop");
        if (desktop) {
            scheduler_add(desktop);
            printf("[init] userspace desktop loaded (pid=%d)\n", desktop->pid);
        } else {
            Process* fallback = init_load_program("/bin/FALLBACK_SHELL.ELF", 2, "fallback-shell");
            if (fallback) {
                scheduler_add(fallback);
                printf("[init] fallback shell loaded (pid=%d)\n", fallback->pid);
            } else {
                printf("[init] userspace desktop unavailable\n");
            }
        }
    } else {
        Process* fallback = init_load_program("/bin/FALLBACK_SHELL.ELF", 2, "fallback-shell");
        if (fallback) {
            scheduler_add(fallback);
            printf("[init] fallback shell loaded (pid=%d)\n", fallback->pid);
        }
    }

    __asm__ volatile("sti");
    scheduler_start();
    for (;;) __asm__ volatile("sti; hlt");
}