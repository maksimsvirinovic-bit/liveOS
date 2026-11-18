#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
typedef struct SystemConfig {
    // SYSTEM
    char     hostname[64];
    char     timezone[32];
    char     boot_mode[16];
    char     version[32];

    // SYSTEM/triggers
    uint32_t on_kernel_crash;
    char     on_starter_crash[256];
    uint32_t on_oom;
    uint32_t on_fs_corruption;
    char     on_boot_complete[256];
    char     on_shutdown[256];
    uint32_t panic_qr_code;

    // SYSTEM/resolution
    char     mrules[256];
    uint32_t vga_width;
    uint32_t vga_height;
    uint32_t vga_bpp;
    uint32_t font_scale;

    // SYSTEM/boot
    char     kernel_path[256];
    char     initramfs_path[256];
    char     cmdline[256];
    uint32_t timeout_sec;
    uint32_t debug_early;
    uint32_t safe_mode;

    // SYSTEM/security
    uint32_t allow_user_mode;
    uint32_t kaslr;
    char     selinux_mode[16];

    // SYSTEM/logging
    uint32_t log_level;
    char     log_output[32];
    char     log_path[256];
    uint32_t log_max_size_kb;
    uint32_t log_rotate_count;
    char     crash_dump_path[256];

    // SYSTEM/power
    uint32_t shutdown_timeout_ms;
    uint32_t reboot_on_crash;
    uint32_t safe_mode_on_boot;

    // DRIVERS/nvme
    uint32_t nvme_enabled;
    char     nvme_irq_mode[16];
    uint32_t nvme_queue_depth;
    uint32_t nvme_timeout_ms;
    uint32_t nvme_bounce_kb;

    // DRIVERS/ps2
    uint32_t ps2_enabled;
    char     ps2_kbd_layout[16];
    uint32_t ps2_repeat_delay_ms;

    // DRIVERS/e1000
    uint32_t e1000_enabled;
    uint32_t e1000_dhcp;

    // USER/default
    char     default_shell[256];
    char     default_home[256];
    char     default_path[256];
    char     default_lang[32];
    char     default_keyboard_layout[16];

    // USER/desktop
    uint32_t desktop_autostart;

    // Metadata
    int      loaded;
} SystemConfig;
extern SystemConfig g_config;
int  config_load(void);
void config_defaults(void);
void config_dump(void);

#ifdef __cplusplus
}
#endif

#endif