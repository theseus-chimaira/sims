/*
 * $Id: ws.h,v 1.17 2004/02/03 21:23:51 phil Exp $
 * Interfaces to window-system specific code for XY display simulation
 */

/*
 * Copyright (c) 2003-2004, Philip L. Budne
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

/* unless you're writing a new driver, you shouldn't be looking here! */

#include <stdint.h>             /* uint32_t for ws_write_bmp1() */

extern int ws_init(const char *, int, int, int, void *);
void ws_shutdown(void);

/*
 * Headless "shadow framebuffer" support.
 *
 * ws_headless selects whether the display library drives a real SDL
 * window (0) or renders only into the in-memory surface[] shadow
 * framebuffer (non-zero).  It defaults to headless so that binaries
 * built against an SDL with no usable graphics frontend do not crash
 * when a display device is enabled.  A device SET routine may clear it
 * (e.g. "SET DPY GUI") before the display is first initialized to open
 * a real window where one is available.
 *
 * ws_screenshot() writes the current contents of the shadow
 * framebuffer to a 1-bit (monochrome) BMP file.  It works in both
 * headless and windowed modes.  Returns an SCPE_ status code.
 */
extern int ws_headless;
extern int ws_screenshot(const char *filename);

/*
 * Write an arbitrary monochrome framebuffer (top-origin, one uint32 per
 * pixel) to a 1-bit BMP file.  Pixels not equal to "background" are
 * emitted as set.  Usable by devices that maintain their own surfaces
 * outside the display library (e.g. the Data Disc video switch).
 */
extern int ws_write_bmp1(const char *filename, const uint32_t *fb, int w, int h, uint32_t background);
extern void *ws_color_rgb(int, int, int);
extern void *ws_color_black(void);
extern void *ws_color_white(void);
extern void ws_display_point(int, int, void *);
extern void ws_sync(void);
extern int ws_poll(int *, int);
extern void ws_beep(void);

/* entries into display.c from below: */
extern void display_keyup(int);
extern void display_keydown(int);
extern void display_repaint(void);

/*
 * Globals set by O/S display level to SCALED location in display
 * coordinate system in order to save an upcall on every mouse
 * movement.
 *
 * *NOT* for consumption by clients of display.c; although display
 * clients can now get the scaling factor, real displays only give you
 * a light pen "hit" when the beam passes under the light pen.
 */

extern int ws_lp_x, ws_lp_y;

/*
 * O/S services in theory independent of window system,
 * but in (current) practice not!
 */
extern unsigned long os_elapsed(void);
