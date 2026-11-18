#ifndef PS2_H
#define PS2_H

#include <stdint.h>

typedef struct {
	int32_t x;
	int32_t y;
	uint32_t buttons;
} PointerState;

void ps2_init(void);
void ps2_set_pointer_bounds(uint32_t width, uint32_t height);
void ps2_get_pointer_state(PointerState* state);

int  kbd_getchar(void);
int  kbd_haschar(void);
void kbd_set_tty_focus(int enabled);
int  kbd_tty_focus(void);
void tty_clear_input(void);
void tty_reset(void);

#define KBD_KEY_UP      (-10)
#define KBD_KEY_DOWN    (-11)
#define KBD_KEY_LEFT    (-12)
#define KBD_KEY_RIGHT   (-13)
#define KBD_KEY_HOME    (-14)
#define KBD_KEY_END     (-15)
#define KBD_KEY_PGUP    (-16)
#define KBD_KEY_PGDN    (-17)
#define KBD_KEY_DELETE  (-18)
#define KBD_KEY_INSERT  (-19)
#define KBD_KEY_F1      (-20)
#define KBD_KEY_F2      (-21)
#define KBD_KEY_F3      (-22)
#define KBD_KEY_F4      (-23)
#define KBD_KEY_F5      (-24)
#define KBD_KEY_F6      (-25)
#define KBD_KEY_F7      (-26)
#define KBD_KEY_F8      (-27)
#define KBD_KEY_F9      (-28)
#define KBD_KEY_F10     (-29)
#define KBD_KEY_F11     (-30)
#define KBD_KEY_F12     (-31)
#define KBD_KEY_ESC     (-40)
#define KBD_KEY_BACKSPACE (-41)
#define KBD_KEY_TAB     (-42)
#define KBD_KEY_ENTER   (-43)
#define KBD_KEY_LCTRL   (-44)
#define KBD_KEY_LSHIFT  (-45)
#define KBD_KEY_LALT    (-46)
#define KBD_KEY_CAPS    (-47)

int input_getchar_blocking(void);

#endif