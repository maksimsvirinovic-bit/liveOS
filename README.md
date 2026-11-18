# LiveOS

LiveOS — экспериментальная 64-разрядная ОС для x86_64 с UEFI-загрузчиком, ядром, FAT32/regfs и графическим desktop на пользовательском compositor.

Полная документация по архитектуре, подсистемам, syscall/IPC, сборке и ограничениям: [DOCUMENTATION.md](DOCUMENTATION.md).

Краткие команды:

```sh
make
bash user/build.sh
make install-user-programs
make run-kvm
```

Сборка user ELF и их установка в `disk.img` — отдельный шаг от сборки ISO. Для установки нужен доступ `sudo` к loop mount; перед запуском QEMU убедитесь, что другой процесс не держит `disk.img`.
