#!/bin/bash
set -euo pipefail

USER_DIR=user
DESKTOP_DIR=$USER_DIR/gui_addon
BUILD=build/user
INSTALL_TO_DISK=${1:-}

mkdir -p "$BUILD"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/_start.o" "$USER_DIR/_start.S" \
    -ffreestanding -nostdlib -fno-pic -fno-pie

build_program() {
    local source=$1
    local output=$2
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/$output.o" "$USER_DIR/$source.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
    ld -m elf_x86_64 -T "$USER_DIR/linker.ld" -o "$BUILD/$output.ELF" \
        "$BUILD/_start.o" "$BUILD/$output.o" "$BUILD/libc.o" "$BUILD/malloc.o"
}

build_gui_program() {
    local source=$1
    local output=$2
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/$output.o" "$USER_DIR/$source.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/gui.o" "$DESKTOP_DIR/gui.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/render.o" "$DESKTOP_DIR/render.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/user_font.o" kernel/graphics/font_8x8.c \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx
    ld -m elf_x86_64 -T "$USER_DIR/linker.ld" -o "$BUILD/$output.ELF" \
        "$BUILD/_start.o" "$BUILD/$output.o" "$BUILD/gui.o" "$BUILD/render.o" "$BUILD/user_font.o" "$BUILD/libc.o" "$BUILD/malloc.o"
}

x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/libc.o" "$USER_DIR/libc.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/malloc.o" "$USER_DIR/malloc.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"

build_program SHELL SH
build_program LS LS
build_program CAT CAT
build_program MKDIR MKDIR
build_program RMDIR RMDIR
build_program RM RM
build_program INIT INIT
#build_gui_program FILES FILES
#build_gui_program desktop/apps/notes_app NOTES
#build_gui_program desktop/apps/registry_app REGISTRY
#build_gui_program desktop/apps/settings_app SETTINGS
#build_gui_program desktop/apps/fonts_app FONTS
build_program FALLBACK_SHELL FALLBACK_SHELL
build_program NETTEST NETTEST
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/PKG.o" "$USER_DIR/PKG.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/zip.o" "$USER_DIR/zip.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR"
ld -m elf_x86_64 -T "$USER_DIR/linker.ld" -o "$BUILD/PKG.ELF" \
    "$BUILD/_start.o" "$BUILD/PKG.o" "$BUILD/zip.o" "$BUILD/libc.o" "$BUILD/malloc.o"

DESKTOP_DIR=$USER_DIR/desktop
GUI_DIR=$USER_DIR/gui_addon

# общий gui + render + window_server (один раз)
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/gui.o" "$GUI_DIR/gui.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR" -I"$GUI_DIR" -I"$DESKTOP_DIR"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/render.o" "$GUI_DIR/render.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR" -I"$GUI_DIR"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/window_server.o" "$GUI_DIR/window_server.c" \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR" -I"$GUI_DIR"
x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/user_font.o" kernel/graphics/font_8x8.c \
    -ffreestanding -nostdlib -fno-pic -fno-pie \
    -mno-red-zone -fno-stack-protector \
    -mno-sse -mno-sse2 -mno-mmx

for src in main app_launcher; do
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/desktop_$src.o" "$DESKTOP_DIR/$src.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR" -I"$GUI_DIR" -I"$DESKTOP_DIR"
done

mkdir -p "$BUILD/builtin"

for src in builtin/terminal builtin/ui_studio; do
    x86_64-linux-gnu-gcc -c -O0 -o "$BUILD/$src.o" "$DESKTOP_DIR/$src.c" \
        -ffreestanding -nostdlib -fno-pic -fno-pie \
        -mno-red-zone -fno-stack-protector \
        -mno-sse -mno-sse2 -mno-mmx -I"$USER_DIR" -I"$GUI_DIR" -I"$DESKTOP_DIR"
done

ld -m elf_x86_64 -T "$USER_DIR/linker.ld" -o "$BUILD/DESKTOP.ELF" \
    "$BUILD/_start.o" \
    "$BUILD/desktop_main.o" \
    "$BUILD/desktop_app_launcher.o" \
    "$BUILD/builtin/terminal.o" \
    "$BUILD/builtin/ui_studio.o" \
    "$BUILD/gui.o" "$BUILD/render.o" "$BUILD/window_server.o" \
    "$BUILD/user_font.o" "$BUILD/libc.o" "$BUILD/malloc.o"

if [[ "$INSTALL_TO_DISK" == "--install-disk" ]]; then
    MOUNT_DIR=$(mktemp -d)
    cleanup() {
        sudo umount "$MOUNT_DIR" 2>/dev/null || true
        rmdir "$MOUNT_DIR" 2>/dev/null || true
    }
    trap cleanup EXIT

    sudo mount -o loop disk.img "$MOUNT_DIR"
    sudo mkdir -p "$MOUNT_DIR/bin" "$MOUNT_DIR/etc" "$MOUNT_DIR/usr/fonts"
    for program in SH LS CAT MKDIR RMDIR RM INIT FILES NOTES REGISTRY SETTINGS FONTS DESKTOP NETTEST PKG; do
        sudo cp "$BUILD/$program.ELF" "$MOUNT_DIR/bin/"
    done
fi
