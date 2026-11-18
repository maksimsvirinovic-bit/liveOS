#include "config.h"
#include "regfs.h"
#include "fs.h"
#include "serial/serial.h"
#include "str.h"
#include <stddef.h>

SystemConfig g_config;

void config_defaults(void) {
    memset(&g_config, 0, sizeof(g_config));

    strcpy(g_config.hostname, "liveos");
    strcpy(g_config.timezone, "UTC");
    strcpy(g_config.boot_mode, "normal");
    strcpy(g_config.version, "0.1");

    g_config.on_kernel_crash = 0x01;
    strcpy(g_config.on_starter_crash, "/bin/starter");
    g_config.on_oom = 0x01;
    g_config.on_fs_corruption = 0x00;
    strcpy(g_config.on_boot_complete, "/etc/init.sh");
    strcpy(g_config.on_shutdown, "/etc/shutdown.sh");
    g_config.panic_qr_code = 0x01;

    strcpy(g_config.mrules, "/etc/mrules.conf");
    g_config.vga_width = 1024;
    g_config.vga_height = 768;
    g_config.vga_bpp = 32;
    g_config.font_scale = 1;

    strcpy(g_config.kernel_path, "/boot/kernel.elf");
    strcpy(g_config.initramfs_path, "/boot/initramfs.img");
    strcpy(g_config.cmdline, "");
    g_config.timeout_sec = 5;
    g_config.debug_early = 0;
    g_config.safe_mode = 0;

    g_config.allow_user_mode = 1;
    g_config.kaslr = 0;
    strcpy(g_config.selinux_mode, "off");

    g_config.log_level = 3;
    strcpy(g_config.log_output, "serial");
    strcpy(g_config.log_path, "/var/log/");
    g_config.log_max_size_kb = 1024;
    g_config.log_rotate_count = 3;
    strcpy(g_config.crash_dump_path, "/var/crash/");

    g_config.shutdown_timeout_ms = 10000;
    g_config.reboot_on_crash = 0;
    g_config.safe_mode_on_boot = 0;

    g_config.nvme_enabled = 1;
    strcpy(g_config.nvme_irq_mode, "intx");
    g_config.nvme_queue_depth = 32;
    g_config.nvme_timeout_ms = 5000;
    g_config.nvme_bounce_kb = 64;

    g_config.ps2_enabled = 1;
    strcpy(g_config.ps2_kbd_layout, "us");
    g_config.ps2_repeat_delay_ms = 500;

    g_config.e1000_enabled = 1;
    g_config.e1000_dhcp = 1;

    strcpy(g_config.default_shell, "/bin/SH.ELF");
    strcpy(g_config.default_home, "/");
    strcpy(g_config.default_path, "/bin:/usr/bin");
    strcpy(g_config.default_lang, "en_US");
    strcpy(g_config.default_keyboard_layout, "us");

    g_config.desktop_autostart = 1;

    g_config.loaded = 0;
}

int config_load(void) {
    FileSystem* regfs_fs = fs_find_by_path("/reg/SYSTEM.HIV");
    if (!regfs_fs || !regfs_fs->private) {
        serial_emergency_print("config: regfs not mounted, using defaults\n");
        config_defaults();
        return -1;
    }
    RegFS* rfs = (RegFS*)regfs_fs->private;

    config_defaults();

    regfs_get_str(rfs, "SYSTEM/hostname", g_config.hostname, sizeof(g_config.hostname));
    regfs_get_str(rfs, "SYSTEM/timezone", g_config.timezone, sizeof(g_config.timezone));
    regfs_get_str(rfs, "SYSTEM/boot_mode", g_config.boot_mode, sizeof(g_config.boot_mode));
    regfs_get_str(rfs, "SYSTEM/version", g_config.version, sizeof(g_config.version));

    g_config.on_kernel_crash = regfs_get_u32(rfs, "SYSTEM/triggers/on_kernel_crash");
    regfs_get_str(rfs, "SYSTEM/triggers/on_starter_crash",
                  g_config.on_starter_crash, sizeof(g_config.on_starter_crash));
    g_config.on_oom = regfs_get_u32(rfs, "SYSTEM/triggers/on_oom");
    g_config.on_fs_corruption = regfs_get_u32(rfs, "SYSTEM/triggers/on_fs_corruption");
    regfs_get_str(rfs, "SYSTEM/triggers/on_boot_complete",
                  g_config.on_boot_complete, sizeof(g_config.on_boot_complete));
    regfs_get_str(rfs, "SYSTEM/triggers/on_shutdown",
                  g_config.on_shutdown, sizeof(g_config.on_shutdown));
    g_config.panic_qr_code = regfs_get_u32(rfs, "SYSTEM/triggers/panic_qr_code");

    regfs_get_str(rfs, "SYSTEM/resolution/mrules",
                  g_config.mrules, sizeof(g_config.mrules));
    g_config.vga_width  = regfs_get_u32(rfs, "SYSTEM/resolution/vga_width");
    g_config.vga_height = regfs_get_u32(rfs, "SYSTEM/resolution/vga_height");
    g_config.vga_bpp    = regfs_get_u32(rfs, "SYSTEM/resolution/vga_bpp");
    g_config.font_scale = regfs_get_u32(rfs, "SYSTEM/resolution/font_scale");

    regfs_get_str(rfs, "SYSTEM/boot/kernel_path",
                  g_config.kernel_path, sizeof(g_config.kernel_path));
    regfs_get_str(rfs, "SYSTEM/boot/initramfs_path",
                  g_config.initramfs_path, sizeof(g_config.initramfs_path));
    regfs_get_str(rfs, "SYSTEM/boot/cmdline",
                  g_config.cmdline, sizeof(g_config.cmdline));
    g_config.timeout_sec = regfs_get_u32(rfs, "SYSTEM/boot/timeout_sec");
    g_config.debug_early = regfs_get_u32(rfs, "SYSTEM/boot/debug_early");
    g_config.safe_mode   = regfs_get_u32(rfs, "SYSTEM/boot/safe_mode");

    g_config.allow_user_mode = regfs_get_u32(rfs, "SYSTEM/security/allow_user_mode");
    g_config.kaslr = regfs_get_u32(rfs, "SYSTEM/security/kaslr");
    regfs_get_str(rfs, "SYSTEM/security/selinux_mode",
                  g_config.selinux_mode, sizeof(g_config.selinux_mode));

    g_config.log_level = regfs_get_u32(rfs, "SYSTEM/logging/level");
    serial_set_debug_enabled(regfs_get_u32(rfs, "SYSTEM/logging/serial_enabled") != 0);
    regfs_get_str(rfs, "SYSTEM/logging/output",
                  g_config.log_output, sizeof(g_config.log_output));
    regfs_get_str(rfs, "SYSTEM/logging/log_path",
                  g_config.log_path, sizeof(g_config.log_path));
    g_config.log_max_size_kb = regfs_get_u32(rfs, "SYSTEM/logging/max_size_kb");
    g_config.log_rotate_count = regfs_get_u32(rfs, "SYSTEM/logging/rotate_count");
    regfs_get_str(rfs, "SYSTEM/logging/crash_dump_path",
                  g_config.crash_dump_path, sizeof(g_config.crash_dump_path));

    g_config.shutdown_timeout_ms = regfs_get_u32(rfs, "SYSTEM/power/shutdown_timeout_ms");
    g_config.reboot_on_crash = regfs_get_u32(rfs, "SYSTEM/power/reboot_on_crash");
    g_config.safe_mode_on_boot = regfs_get_u32(rfs, "SYSTEM/power/safe_mode_on_boot");

    g_config.nvme_enabled = regfs_get_u32(rfs, "DRIVERS/nvme/enabled");
    regfs_get_str(rfs, "DRIVERS/nvme/irq_mode",
                  g_config.nvme_irq_mode, sizeof(g_config.nvme_irq_mode));
    g_config.nvme_queue_depth = regfs_get_u32(rfs, "DRIVERS/nvme/queue_depth");
    g_config.nvme_timeout_ms = regfs_get_u32(rfs, "DRIVERS/nvme/timeout_ms");
    g_config.nvme_bounce_kb = regfs_get_u32(rfs, "DRIVERS/nvme/bounce_buffer_kb");

    g_config.ps2_enabled = regfs_get_u32(rfs, "DRIVERS/ps2/enabled");
    regfs_get_str(rfs, "DRIVERS/ps2/kbd_layout",
                  g_config.ps2_kbd_layout, sizeof(g_config.ps2_kbd_layout));
    g_config.ps2_repeat_delay_ms = regfs_get_u32(rfs, "DRIVERS/ps2/repeat_delay_ms");

    g_config.e1000_enabled = regfs_get_u32(rfs, "DRIVERS/e1000/enabled");
    g_config.e1000_dhcp = regfs_get_u32(rfs, "DRIVERS/e1000/dhcp");

    regfs_get_str(rfs, "USER/default/shell",
                  g_config.default_shell, sizeof(g_config.default_shell));
    regfs_get_str(rfs, "USER/default/home",
                  g_config.default_home, sizeof(g_config.default_home));
    regfs_get_str(rfs, "USER/default/path",
                  g_config.default_path, sizeof(g_config.default_path));
    regfs_get_str(rfs, "USER/default/lang",
                  g_config.default_lang, sizeof(g_config.default_lang));
    regfs_get_str(rfs, "USER/default/keyboard_layout",
                  g_config.default_keyboard_layout, sizeof(g_config.default_keyboard_layout));

    g_config.desktop_autostart = regfs_get_u32(rfs, "USER/desktop/autostart");

    g_config.loaded = 1;

    serial_emergency_print("config: loaded from regfs\n");
    return 0;
}

void config_dump(void) {
    serial_emergency_print("=== CONFIG DUMP ===\n");
    serial_emergency_print("hostname = ");     serial_emergency_print(g_config.hostname);     serial_emergency_print("\n");
    serial_emergency_print("boot_mode = ");    serial_emergency_print(g_config.boot_mode);    serial_emergency_print("\n");
    serial_emergency_print("on_kernel_crash = "); serial_emergency_print_hex(g_config.on_kernel_crash); serial_emergency_print("\n");
    serial_emergency_print("on_starter_crash = "); serial_emergency_print(g_config.on_starter_crash); serial_emergency_print("\n");
    serial_emergency_print("vga_width = ");    serial_emergency_print_hex(g_config.vga_width); serial_emergency_print("\n");
    serial_emergency_print("vga_height = ");   serial_emergency_print_hex(g_config.vga_height); serial_emergency_print("\n");
    serial_emergency_print("nvme_enabled = "); serial_emergency_print_hex(g_config.nvme_enabled); serial_emergency_print("\n");
    serial_emergency_print("nvme_queue_depth = "); serial_emergency_print_hex(g_config.nvme_queue_depth); serial_emergency_print("\n");
    serial_emergency_print("ps2_enabled = ");  serial_emergency_print_hex(g_config.ps2_enabled); serial_emergency_print("\n");
    serial_emergency_print("default_shell = "); serial_emergency_print(g_config.default_shell); serial_emergency_print("\n");
    serial_emergency_print("desktop_autostart = "); serial_emergency_print_hex(g_config.desktop_autostart); serial_emergency_print("\n");
    serial_emergency_print("=== END CONFIG ===\n");
}