# LiveOS

LiveOS — экспериментальная 64-разрядная ОС для x86_64 с UEFI-загрузчиком, ядром, FAT32/regfs и графическим desktop на пользовательском compositor.

Полная документация по архитектуре, подсистемам, syscall/IPC, сборке и ограничениям: [DOCUMENTATION.md](DOCUMENTATION.md).

Перед сборкой надо поставить зависимости:

### Сборка (ядро + ISO)
- `x86_64-linux-gnu-gcc`, `x86_64-linux-gnu-g++`, `x86_64-linux-gnu-ld` — кросс-тулчейн
- `nasm` — ассемблер
- `make`, `python3`, `git` — сборка
- `dosfstools` (`mkfs.fat`), `mtools` (`mmd`, `mcopy`), `xorriso` — FAT-образ и ISO
- `gcc`, `uuid-dev`, `acpica-tools` (`iasl`) — сборка EFI-загрузчика (edk2)

### Запуск
- `qemu-system-x86_64`, `ovmf` (UEFI-прошивка)
- для `make run-kvm` — доступ к `/dev/kvm` (группа `kvm`)

### Тесты
- `make test-net`, `test-zip`, `test-tls` — только нативный `gcc`, кросс не нужен

### Установка (Debian/Ubuntu)
```bash
sudo apt install -y \
  gcc-x86-64-linux-gnu g++-x86-64-linux-gnu binutils-x86-64-linux-gnu \
  nasm gcc g++ make python3 git uuid-dev acpica-tools \
  dosfstools mtools xorriso qemu-system-x86 ovmf
```

### Arch
```bash
sudo pacman -S --needed \
  nasm gcc make \
  python git util-linux \
  dosfstools mtools xorriso \
  qemu-full edk2-ovmf \
  acpica
yay -S x86_64-linux-gnu-gcc x86_64-linux-gnu-binutils
```

Краткие команды:

```sh
dd if=/dev/zero of=disk.img bs=4M count=16
make
bash user/build.sh
make install-user-programs
make run-kvm
```

Можно собрать без кросс-компилятора, достаточно поменять в Makefile cc и cxx на установленный компилятор для c и c++ соотв. и ld на установленный линкер.
