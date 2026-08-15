/*
 *  Copyright (C) 2026 TenoxVNC.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * xwidgets.h - the small set of controls the viewer draws for itself.
 *
 * Enough of a toolkit for the connection dialog and the F8 menu: labels,
 * text fields, checkboxes, radio buttons, push buttons, sliders and
 * separators, drawn with Xlib on a bare Xt core widget, so the viewer needs
 * nothing beyond Xt itself.
 *
 * A panel owns its items, so the F8 menu can stay around between uses while
 * a modal dialog comes and goes.
 */

#ifndef XWIDGETS_H
#define XWIDGETS_H

#define XW_MAXITEMS 48

enum {				/* item kinds */
  XW_LABEL,
  XW_TEXT,
  XW_CHECK,
  XW_RADIO,
  XW_BUTTON,
  XW_SLIDER,
  XW_SEP
};

typedef struct {
  int kind;
  const char *label;
  int x, y, w, h;

  char *buf;			/* XW_TEXT */
  int maxlen;
  int caret;
  Bool secret;
  Bool numeric;

  Bool *flag;			/* XW_CHECK, when it tracks a variable */
  Bool state;			/* XW_CHECK, when the owner sets it */
  Bool disabled;		/* fixed when the panel is built */
  Bool *enableIf;		/* live only while this is set: a slider that
				   only means something when its checkbox is
				   ticked greys itself out when it is not */

  int *num;			/* XW_SLIDER, XW_RADIO: the value being set */
  const int *vals;		/* XW_SLIDER: the notches, in order */
  int nvals;
  Bool autoFirst;		/* vals[0] is "let the viewer decide" */
  int valW;			/* room kept at the right for the value */

  int val;			/* XW_RADIO: what this button stands for; the
				   whole group shares one num, so the one it
				   holds is the one drawn filled in */

  int id;			/* XW_BUTTON and XW_CHECK: result code */
} XwItem;

typedef struct {
  Widget shell, canvas;
  Window win;
  Pixmap buf;
  GC gc;
  int w, h;

  XwItem items[XW_MAXITEMS];
  int nItems;
  int focus;

  int result;			/* id of whatever was activated */
  int drag;			/* slider being dragged, -1 for none */
  Bool done;
  const char *message;		/* shown in red across the top */
  Bool modal;

  /* Modeless panels act on a click straight away through this.  Polling for
     the result instead would not work: when the viewer is idle it waits for
     the server inside sockets.c, which dispatches X events but never returns
     to the main loop. */
  void (*activate)(int id);
} XwPanel;

/* Shared appearance, set up once by XwInit(). */
extern XFontStruct *xwFont;
extern int xwLineH, xwCharW;
extern unsigned long xwBg, xwFg, xwField, xwDark, xwLight, xwWarn;

extern Bool XwInit(void);

extern Bool XwLive(XwItem *it);

extern int XwStrW(const char *s);
extern int XwCheckW(const char *s);

extern void XwReset(XwPanel *p);
extern XwItem *XwAddLabel(XwPanel *p, const char *s, int x, int y);
extern XwItem *XwAddText(XwPanel *p, char *buf, int maxlen, int x, int y,
			 int cols, Bool secret, Bool numeric);
extern XwItem *XwAddCheck(XwPanel *p, const char *s, Bool *flag, int id,
			  int x, int y);
extern XwItem *XwAddRadio(XwPanel *p, const char *s, int *value, int val,
			  int x, int y);
extern XwItem *XwAddButton(XwPanel *p, const char *s, int id, int x, int y);
extern XwItem *XwAddSlider(XwPanel *p, int *value, const int *vals, int nvals,
			   Bool autoFirst, int x, int y, int w);
extern XwItem *XwAddSep(XwPanel *p, int x, int y, int w);
extern int XwContentWidth(XwPanel *p);

extern void XwBuildWindow(XwPanel *p, const char *name, const char *title,
			  int w, int h, Bool modal);
extern void XwRedraw(XwPanel *p);
extern void XwPlaceCentred(XwPanel *p);
extern void XwPlaceAt(XwPanel *p, int x, int y);
extern void XwPopup(XwPanel *p);
extern void XwPopdown(XwPanel *p);
extern void XwDestroy(XwPanel *p);
extern int XwRunModal(XwPanel *p);

#endif /* XWIDGETS_H */
