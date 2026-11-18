# Графика, окна и IPC

## Владельцы ресурсов

Единственный framebuffer/compositor принадлежит процессу `DESKTOP.ELF`. Он создаёт `GuiDesktop`, опрашивает input, вызывает GUI hit testing, рисует окна в backbuffer и публикует кадры. Клиентские программы — отдельные ELF и не должны напрямую очищать framebuffer или создавать независимый desktop root.

## Поток создания окна

1. Desktop регистрирует себя через `SYS_WINDOW_REGISTER`.
2. Приложение вызывает `sys_window_create(title, x, y, width, height)`.
3. Kernel ставит `USER_WINDOW_MSG_CREATE` в очередь для PID desktop.
4. Desktop вызывает `window_server_dispatch_message`, создаёт окно, записывает `owner_pid` и возвращает `window_id` клиенту.
5. Клиент отправляет `USER_WINDOW_MSG_CREATE_CONTROL` для каждого элемента.
6. Desktop создаёт control в общей модели `GuiWindow`; compositor рисует его вместе с остальным desktop.

Схема событий:

```text
ELF-клиент --SYS_WINDOW_POST--> kernel queue --> desktop/window server
ELF-клиент <--SYS_WINDOW_REPLY-- kernel queue <-- desktop/window server
```

Kernel записывает PID отправителя самостоятельно. Desktop использует owner PID для привязки окна и доставки pointer/key/close событий.

## Формат сообщений

`UserWindowMessage` описан в `user/sys.h`; зеркальная структура ядра находится в `kernel/arch/x86_64/syscall.c`. Их layout и размер payload должны совпадать.

Основные типы:

- `CREATE`, `MOVE`, `DESTROY`, `SET_TITLE`;
- `CREATE_CONTROL`, `UPDATE_CONTROL`;
- `INPUT`, `CLOSE`, `REDRAW`.

Поля `window_id`, `control_id`, геометрия, тип control, selection/input и `payload` передают команду. `payload_size` не должен превышать `USER_WINDOW_PAYLOAD`.

`SYS_WINDOW_RECV` для приложений блокируется до сообщения, чтобы не создавать busy polling и переключения планировщика. Desktop вызывает receive в неблокирующем цикле, иначе остановится обработка ввода и отрисовка.

Очередь имеет фиксированную ёмкость. Переполнение обрабатывается вытеснением старейшего элемента; это защита от блокировки, но не полноценная гарантия доставки. Большие серии изменений списка лучше объединять или передавать пакетно.

## Controls и события

GUI типы описаны в `user/desktop/gui.h`: label, button, textfield, panel, list, textarea и canvas. Controls создаются с локальной геометрией относительно окна. Desktop hit-tests pointer, обновляет focus/selection и отправляет event владельцу окна. Клиент должен обрабатывать input event и отправлять `UPDATE_CONTROL`, когда меняется отображаемый текст/список.

Закрытие titlebar вызывает `USER_WINDOW_MSG_CLOSE` владельцу. Приложение должно завершать процесс только после close-события или явной ошибки создания окна. При завершении kernel удаляет адресованные процессу IPC сообщения.

## Renderer и производительность

Renderer в `user/desktop/render.c` держит backbuffer и отображает его в framebuffer. Полный present копирует все пиксели; desktop ограничивает частоту полного кадра. Cursor overlay и drag работают через dirty rectangles/snapshot.

GPU backend пока отсутствует. Производительность зависит от числа перерисовок, объёма framebuffer-копирования и количества controls. При добавлении новых виджетов необходимо избегать полного кадра на каждое мелкое изменение.

## Текущие ограничения приложений

Отдельный ELF и окно не означают, что все сценарии приложения завершены. Некоторые клиенты создают и отображают controls, но обработчики кнопок/редактирование текста могут быть неполными. Проверяйте каждое действие отдельно: создание, input event, изменение control, close и повторный запуск.
