/*
 * Copyright (c) 2017 SUSE LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "main.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* links
 * https://www.kernel.org/doc/Documentation/usb/gadget_configfs.txt
 * https://www.kernel.org/doc/Documentation/usb/gadget_hid.txt
 * https://docs.mbed.com/docs/ble-hid/en/latest/api/md_doc_HID.html
 * https://github.com/libusbgx/libusbgx
 */

static int _fd = -1;

static struct {
    struct {
	uint8_t ctrl_l:1;
	uint8_t shift_l:1;
	uint8_t alt_l:1;
	uint8_t gui_l:1;
	uint8_t ctrl_r:1;
	uint8_t shift_r:1;
	uint8_t alt_r:1;
	uint8_t gui_r:1;
    } mod;
    uint8_t res;
    uint8_t key[6];
} keystate;

int usbhid_init(const char* fn)
{
    _fd = open (fn, O_RDWR | O_NOCTTY);
    if (_fd < 0) {
	fprintf(stderr, "failed to open %s: %m\n", fn);
	exit(EXIT_FAILURE);
    }
    return 0;
}

/* basic keys */
static uint8_t _keymap_latin1_base[XK_asciitilde-XK_space+1] = {
    0x2c, /* XK_space */
    0x1e, /* XK_exclam */
    0x34, /* XK_quotedbl */
    0x20, /* XK_numbersign */
    0x21, /* XK_dollar */
    0x22, /* XK_percent */
    0x24, /* XK_ampersand */
    0x34, /* XK_apostrophe */
    0x26, /* XK_parenleft */
    0x27, /* XK_parenright */
    0x25, /* XK_asterisk */
    0x2e, /* XK_plus */
    0x36, /* XK_comma */
    0x2d, /* XK_minus */
    0x37, /* XK_period */
    0x38, /* XK_slash */
    0x27, /* XK_0 */
    0x1e, /* XK_1 */
    0x1f, /* XK_2 */
    0x20, /* XK_3 */
    0x21, /* XK_4 */
    0x22, /* XK_5 */
    0x23, /* XK_6 */
    0x24, /* XK_7 */
    0x25, /* XK_8 */
    0x26, /* XK_9 */
    0x33, /* XK_colon */
    0x33, /* XK_semicolon */
    0x36, /* XK_less */
    0x2e, /* XK_equal */
    0x37, /* XK_greater */
    0x38, /* XK_question */
    0x1f, /* XK_at */
    0x04, /* XK_A */
    0x05, /* XK_B */
    0x06, /* XK_C */
    0x07, /* XK_D */
    0x08, /* XK_E */
    0x09, /* XK_F */
    0x0a, /* XK_G */
    0x0b, /* XK_H */
    0x0c, /* XK_I */
    0x0d, /* XK_J */
    0x0e, /* XK_K */
    0x0f, /* XK_L */
    0x10, /* XK_M */
    0x11, /* XK_N */
    0x12, /* XK_O */
    0x13, /* XK_P */
    0x14, /* XK_Q */
    0x15, /* XK_R */
    0x16, /* XK_S */
    0x17, /* XK_T */
    0x18, /* XK_U */
    0x19, /* XK_V */
    0x1a, /* XK_W */
    0x1b, /* XK_X */
    0x1c, /* XK_Y */
    0x1d, /* XK_Z */
    0x2f, /* XK_bracketleft */
    0x31, /* XK_backslash */
    0x30, /* XK_bracketright */
    0x23, /* XK_asciicircum */
    0x2d, /* XK_underscore */
    0x35, /* XK_grave */
    0x04, /* XK_a */
    0x05, /* XK_b */
    0x06, /* XK_c */
    0x07, /* XK_d */
    0x08, /* XK_e */
    0x09, /* XK_f */
    0x0a, /* XK_g */
    0x0b, /* XK_h */
    0x0c, /* XK_i */
    0x0d, /* XK_j */
    0x0e, /* XK_k */
    0x0f, /* XK_l */
    0x10, /* XK_m */
    0x11, /* XK_n */
    0x12, /* XK_o */
    0x13, /* XK_p */
    0x14, /* XK_q */
    0x15, /* XK_r */
    0x16, /* XK_s */
    0x17, /* XK_t */
    0x18, /* XK_u */
    0x19, /* XK_v */
    0x1a, /* XK_w */
    0x1b, /* XK_x */
    0x1c, /* XK_y */
    0x1d, /* XK_z */
    0x2f, /* XK_braceleft */
    0x31, /* XK_bar */
    0x30, /* XK_braceright */
    0x35, /* XK_asciitilde */
};

void usbhid_handle_key(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    uint8_t code = 0;

    if (_fd == -1)
	return;

    fprintf(stderr, "key %s 0x%04x\n", down?"press":"release", key);
    switch (key) {
	/* 0x020 */
	case XK_space ... XK_asciitilde:
	    code = _keymap_latin1_base[key-XK_space];
	    break;
	/* 0xFFBE */
	case XK_F1 ... XK_F12:
	    code = key - XK_F1 + 0x3a;
	    break;
	/* 0xFFE1 */
	case XK_Shift_L:
	    keystate.mod.shift_l = down;
	    break;
	case XK_Shift_R:
	    keystate.mod.shift_r = down;
	    break;
	case XK_Control_L:
	    keystate.mod.ctrl_l = down;
	    break;
	case XK_Control_R:
	    keystate.mod.ctrl_r = down;
	    break;
	/* 0xFFE9 */
	case XK_Alt_L:
	    keystate.mod.alt_l = down;
	    break;
	case XK_Alt_R:
	    keystate.mod.alt_r = down;
	    break;
	/* 0xFF08 */
	case XK_BackSpace:
	    code = 0x2a;
	    break;
	case XK_Tab:
	    code = 0x2b;
	    break;
	case XK_Return:
	    code = 0x28;
	    break;
	case XK_Escape:
	    code = 0x29;
	    break;
	case XK_Delete:
	    code = 0x4c;
	    break;
	/* 0xFF50 */
	case XK_Home:
	    code = 0x4a;
	    break;
	case XK_Left:
	    code = 0x50;
	    break;
	case XK_Up:
	    code = 0x52;
	    break;
	case XK_Right:
	    code = 0x4f;
	    break;
	case XK_Down:
	    code = 0x51;
	    break;
	case XK_Prior:
	    code = 0x4b;
	    break;
	case XK_Next:
	    code = 0x4e;
	    break;
	case XK_End:
	    code = 0x4d;
	    break;
	case XK_Begin:
	    code = 0x4a;
	    break;
	/* 0xFF63 */
	case XK_Insert:
	    code = 0x49;
	    break;
	default:
	    fprintf(stderr, "unhandled key %s 0x%04x\n", down?"press":"release", key);
	    return;
    }

    if (code) {
	int i;
	for (i = 0; i < sizeof(keystate.key); ++i) {
	    if (down && keystate.key[i] == 0) {
		keystate.key[i] = code;
		break;
	    }
	    if (!down && keystate.key[i] == code) {
		keystate.key[i] = 0;
		break;
	    }
	}
	if (i == sizeof(keystate.key)) {
	    if (down)
		fputs("too many keys pressed\n", stderr);
	    else
		fputs("key not found\n", stderr);
	}
    }

    write(_fd, &keystate, sizeof(keystate));
}

void usbhid_close()
{
    close(_fd);
}

// vim:sw=4
