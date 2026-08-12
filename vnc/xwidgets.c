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
 * xwidgets.c - the controls the viewer draws for itself.  See xwidgets.h.
 *
 * Everything is drawn into an offscreen pixmap and copied over in one go, so
 * nothing flickers while a field is being typed into.  Panels deliberately
 * use the default visual and colormap: by the time the F8 menu is used the
 * desktop may be on a private BGR233 map in which these colours would mean
 * nothing.
 */

#include "vncviewer.h"
#include "xwidgets.h"
#include <X11/Xutil.h>
#include <ctype.h>

XFontStruct *xwFont;
int xwLineH, xwCharW;
unsigned long xwBg, xwFg, xwField, xwDark, xwLight, xwWarn;

static void XwEvent(Widget w, XtPointer cd, XEvent *ev, Boolean *cont);


static XFontStruct *
XwLoadFont(void)
{
  static const char *names[] = {
    "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1",
    "7x13", "6x13", "8x13", "9x15", "6x10", "fixed", NULL
  };
  XFontStruct *fn;
  int i;

  for (i = 0; names[i]; i++)
    if ((fn = XLoadQueryFont(dpy, names[i])) != NULL)
      return fn;

  return NULL;
}

static unsigned long
XwColour(const char *spec, unsigned long fallback)
{
  XColor screen, exact;
  int scr = DefaultScreen(dpy);

  if (XAllocNamedColor(dpy, DefaultColormap(dpy, scr), spec, &screen, &exact))
    return screen.pixel;

  return fallback;
}

Bool
XwInit(void)
{
  int scr = DefaultScreen(dpy);

  if (xwFont)
    return True;

  xwFont = XwLoadFont();
  if (!xwFont) {
    fprintf(stderr, "%s: no usable font found\n", programName);
    return False;
  }

  xwLineH = xwFont->ascent + xwFont->descent + 2;
  xwCharW = XTextWidth(xwFont, "n", 1);
  if (xwCharW < 1)
    xwCharW = 6;

  xwBg = XwColour("gray85", WhitePixel(dpy, scr));
  xwFg = XwColour("black", BlackPixel(dpy, scr));
  xwField = XwColour("white", WhitePixel(dpy, scr));
  xwDark = XwColour("gray45", BlackPixel(dpy, scr));
  xwLight = XwColour("gray100", WhitePixel(dpy, scr));
  xwWarn = XwColour("red3", BlackPixel(dpy, scr));

  return True;
}


int
XwStrW(const char *s)
{
  return XTextWidth(xwFont, s, strlen(s));
}

/* what a checkbox needs: the box, a gap, then the label */
int
XwCheckW(const char *s)
{
  return xwLineH - 4 + 6 + XwStrW(s);
}


/*
 * Items.
 */

void
XwReset(XwPanel *p)
{
  p->nItems = 0;
  p->focus = -1;
}

static XwItem *
XwAdd(XwPanel *p, int kind, const char *label, int x, int y, int w, int h)
{
  XwItem *it;

  if (p->nItems >= XW_MAXITEMS)
    return &p->items[XW_MAXITEMS - 1];

  it = &p->items[p->nItems++];
  memset(it, 0, sizeof(*it));
  it->kind = kind;
  it->label = label;
  it->x = x;
  it->y = y;
  it->w = w;
  it->h = h;
  return it;
}

XwItem *
XwAddLabel(XwPanel *p, const char *s, int x, int y)
{
  return XwAdd(p, XW_LABEL, s, x, y, XwStrW(s), xwLineH);
}

XwItem *
XwAddText(XwPanel *p, char *buf, int maxlen, int x, int y, int cols,
	  Bool secret, Bool numeric)
{
  XwItem *it = XwAdd(p, XW_TEXT, NULL, x, y, cols * xwCharW + 6, xwLineH + 4);

  it->buf = buf;
  it->maxlen = maxlen;
  it->caret = strlen(buf);
  it->secret = secret;
  it->numeric = numeric;

  if (p->focus < 0)
    p->focus = p->nItems - 1;

  return it;
}

XwItem *
XwAddCheck(XwPanel *p, const char *s, Bool *flag, int id, int x, int y)
{
  XwItem *it = XwAdd(p, XW_CHECK, s, x, y, XwCheckW(s), xwLineH);

  it->flag = flag;
  it->id = id;
  if (flag)
    it->state = *flag;
  return it;
}

XwItem *
XwAddButton(XwPanel *p, const char *s, int id, int x, int y)
{
  XwItem *it = XwAdd(p, XW_BUTTON, s, x, y, XwStrW(s) + 4 * xwCharW,
		     xwLineH + 6);

  it->id = id;
  return it;
}

XwItem *
XwAddSep(XwPanel *p, int x, int y, int w)
{
  return XwAdd(p, XW_SEP, NULL, x, y, w, 2);
}

int
XwContentWidth(XwPanel *p)
{
  int i, cw = 0;

  for (i = 0; i < p->nItems; i++)
    if (p->items[i].x + p->items[i].w > cw)
      cw = p->items[i].x + p->items[i].w;

  return cw;
}


/*
 * Drawing.
 */

static void
XwFrame(XwPanel *p, int x, int y, int w, int h, Bool sunken)
{
  XSetForeground(dpy, p->gc, sunken ? xwDark : xwLight);
  XDrawLine(dpy, p->buf, p->gc, x, y, x + w - 1, y);
  XDrawLine(dpy, p->buf, p->gc, x, y, x, y + h - 1);
  XSetForeground(dpy, p->gc, sunken ? xwLight : xwDark);
  XDrawLine(dpy, p->buf, p->gc, x, y + h - 1, x + w - 1, y + h - 1);
  XDrawLine(dpy, p->buf, p->gc, x + w - 1, y, x + w - 1, y + h - 1);
}

static void
XwDrawItem(XwPanel *p, int i)
{
  XwItem *it = &p->items[i];
  int tx, ty, n, boxY, box;
  const char *s;
  char stars[64];

  ty = it->y + xwFont->ascent;

  switch (it->kind) {

  case XW_LABEL:
    XSetForeground(dpy, p->gc, it->disabled ? xwDark : xwFg);
    XDrawString(dpy, p->buf, p->gc, it->x, ty, it->label, strlen(it->label));
    break;

  case XW_SEP:
    XSetForeground(dpy, p->gc, xwDark);
    XDrawLine(dpy, p->buf, p->gc, it->x, it->y, it->x + it->w - 1, it->y);
    XSetForeground(dpy, p->gc, xwLight);
    XDrawLine(dpy, p->buf, p->gc, it->x, it->y + 1,
	      it->x + it->w - 1, it->y + 1);
    break;

  case XW_TEXT:
    XSetForeground(dpy, p->gc, xwField);
    XFillRectangle(dpy, p->buf, p->gc, it->x, it->y, it->w, it->h);
    XwFrame(p, it->x, it->y, it->w, it->h, True);

    s = it->buf;
    n = strlen(it->buf);
    if (it->secret) {
      for (n = 0; it->buf[n] && n < (int)sizeof(stars) - 1; n++)
	stars[n] = '*';
      stars[n] = '\0';
      s = stars;
    }

    XSetForeground(dpy, p->gc, xwFg);
    XDrawString(dpy, p->buf, p->gc, it->x + 3, it->y + 2 + xwFont->ascent,
		s, n);

    if (i == p->focus) {
      tx = it->x + 3 + XTextWidth(xwFont, s, it->caret);
      XDrawLine(dpy, p->buf, p->gc, tx, it->y + 2, tx, it->y + it->h - 3);
      XDrawRectangle(dpy, p->buf, p->gc, it->x - 1, it->y - 1,
		     it->w + 1, it->h + 1);
    }
    break;

  case XW_CHECK:
    box = xwLineH - 4;
    boxY = it->y + 2;
    XSetForeground(dpy, p->gc, xwField);
    XFillRectangle(dpy, p->buf, p->gc, it->x, boxY, box, box);
    XwFrame(p, it->x, boxY, box, box, True);

    if (it->flag ? *it->flag : it->state) {
      XSetForeground(dpy, p->gc, it->disabled ? xwDark : xwFg);
      XDrawLine(dpy, p->buf, p->gc, it->x + 3, boxY + box / 2,
		it->x + box / 2 - 1, boxY + box - 4);
      XDrawLine(dpy, p->buf, p->gc, it->x + 3, boxY + box / 2 - 1,
		it->x + box / 2 - 1, boxY + box - 5);
      XDrawLine(dpy, p->buf, p->gc, it->x + box / 2 - 1, boxY + box - 4,
		it->x + box - 3, boxY + 3);
      XDrawLine(dpy, p->buf, p->gc, it->x + box / 2 - 1, boxY + box - 5,
		it->x + box - 3, boxY + 2);
    }

    XSetForeground(dpy, p->gc, it->disabled ? xwDark : xwFg);
    XDrawString(dpy, p->buf, p->gc, it->x + box + 6, ty,
		it->label, strlen(it->label));

    if (i == p->focus)
      XDrawRectangle(dpy, p->buf, p->gc, it->x - 2, it->y - 1,
		     it->w + 3, it->h + 1);
    break;

  case XW_BUTTON:
    XSetForeground(dpy, p->gc, xwBg);
    XFillRectangle(dpy, p->buf, p->gc, it->x, it->y, it->w, it->h);
    XwFrame(p, it->x, it->y, it->w, it->h, False);
    XSetForeground(dpy, p->gc, it->disabled ? xwDark : xwFg);
    tx = it->x + (it->w - XwStrW(it->label)) / 2;
    XDrawString(dpy, p->buf, p->gc, tx, it->y + 3 + xwFont->ascent,
		it->label, strlen(it->label));
    if (i == p->focus)
      XDrawRectangle(dpy, p->buf, p->gc, it->x + 2, it->y + 2,
		     it->w - 5, it->h - 5);
    break;
  }
}

void
XwRedraw(XwPanel *p)
{
  int i;

  if (!p->buf)
    return;

  XSetForeground(dpy, p->gc, xwBg);
  XFillRectangle(dpy, p->buf, p->gc, 0, 0, p->w, p->h);

  if (p->message) {
    XSetForeground(dpy, p->gc, xwWarn);
    XDrawString(dpy, p->buf, p->gc, xwCharW * 2,
		xwLineH + xwFont->ascent - 2,
		p->message, strlen(p->message));
  }

  for (i = 0; i < p->nItems; i++)
    XwDrawItem(p, i);

  XCopyArea(dpy, p->buf, p->win, p->gc, 0, 0, p->w, p->h, 0, 0);
}


/*
 * Focus and hit testing.
 */

static Bool
XwFocusable(XwPanel *p, int i)
{
  int k = p->items[i].kind;

  return !p->items[i].disabled &&
	 (k == XW_TEXT || k == XW_CHECK || k == XW_BUTTON);
}

static void
XwMoveFocus(XwPanel *p, int dir)
{
  int i;

  if (p->nItems == 0)
    return;

  for (i = 0; i < p->nItems; i++) {
    p->focus += dir;
    if (p->focus >= p->nItems)
      p->focus = 0;
    if (p->focus < 0)
      p->focus = p->nItems - 1;
    if (XwFocusable(p, p->focus))
      return;
  }
}

static int
XwHit(XwPanel *p, int x, int y)
{
  int i;

  for (i = 0; i < p->nItems; i++) {
    if (!XwFocusable(p, i))
      continue;
    if (x >= p->items[i].x - 2 && x < p->items[i].x + p->items[i].w + 2 &&
	y >= p->items[i].y - 2 && y < p->items[i].y + p->items[i].h + 2)
      return i;
  }
  return -1;
}


/*
 * Text editing.
 */

static void
XwInsert(XwItem *it, char c)
{
  int n = strlen(it->buf);

  if (n >= it->maxlen)
    return;
  if (it->numeric && !isdigit((unsigned char)c))
    return;

  memmove(it->buf + it->caret + 1, it->buf + it->caret, n - it->caret + 1);
  it->buf[it->caret++] = c;
}

static void
XwKey(XwPanel *p, XKeyEvent *ev)
{
  char text[32];
  KeySym ks;
  int n, i, len;
  XwItem *it = p->focus >= 0 ? &p->items[p->focus] : NULL;

  n = XLookupString(ev, text, sizeof(text) - 1, &ks, NULL);

  switch (ks) {

  case XK_Tab:
    XwMoveFocus(p, (ev->state & ShiftMask) ? -1 : 1);
    return;

  /* ISO_Left_Tab arrived in X11R6; on R5 servers such as DECwindows,
     shift-tab comes through as XK_Tab with ShiftMask instead. */
#ifdef XK_ISO_Left_Tab
  case XK_ISO_Left_Tab:
#endif
  case XK_Up:
    XwMoveFocus(p, -1);
    return;

  case XK_Down:
    XwMoveFocus(p, 1);
    return;

  case XK_Return:
  case XK_KP_Enter:
    if (it && it->kind == XW_BUTTON)
      p->result = it->id;
    else if (it && it->kind == XW_CHECK)
      p->result = it->id;
    else
      p->result = p->modal ? 1 : 0;	/* a dialog treats Return as OK */
    if (p->result) {
      if (!p->modal && p->activate)
	p->activate(p->result);
      else
	p->done = True;
    }
    return;

  case XK_Escape:
    p->result = -1;
    if (!p->modal && p->activate)
      p->activate(-1);
    else
      p->done = True;
    return;

  case XK_space:
    if (it && (it->kind == XW_CHECK || it->kind == XW_BUTTON)) {
      p->result = it->id;
      if (!p->modal && p->activate)
	p->activate(p->result);
      else
	p->done = True;
      return;
    }
    break;			/* in a text field a space is just a space */
  }

  if (!it || it->kind != XW_TEXT)
    return;

  len = strlen(it->buf);

  switch (ks) {
  case XK_BackSpace:
    if (it->caret > 0) {
      memmove(it->buf + it->caret - 1, it->buf + it->caret,
	      len - it->caret + 1);
      it->caret--;
    }
    return;
  case XK_Delete:
  case XK_KP_Delete:
    if (it->caret < len)
      memmove(it->buf + it->caret, it->buf + it->caret + 1, len - it->caret);
    return;
  case XK_Left:
    if (it->caret > 0)
      it->caret--;
    return;
  case XK_Right:
    if (it->caret < len)
      it->caret++;
    return;
  case XK_Home:
  case XK_Begin:
    it->caret = 0;
    return;
  case XK_End:
    it->caret = len;
    return;
  }

  if (ev->state & ControlMask) {
    if (ks == XK_u || ks == XK_U) {
      it->buf[0] = '\0';
      it->caret = 0;
    }
    return;
  }

  for (i = 0; i < n; i++)
    if (isprint((unsigned char)text[i]))
      XwInsert(it, text[i]);
}

static void
XwEvent(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{
  XwPanel *p = (XwPanel *)cd;
  int hit;

  switch (ev->type) {

  case Expose:
    if (ev->xexpose.count == 0)
      XwRedraw(p);
    return;

  case ButtonPress:
    hit = XwHit(p, ev->xbutton.x, ev->xbutton.y);
    if (hit < 0)
      return;
    p->focus = hit;

    if (p->items[hit].kind == XW_TEXT) {
      XwItem *it = &p->items[hit];
      int cx = ev->xbutton.x - it->x - 3;
      int len = strlen(it->buf);
      int k;

      it->caret = len;
      for (k = 0; k <= len; k++) {
	int px = it->secret ? k * XTextWidth(xwFont, "*", 1)
			    : XTextWidth(xwFont, it->buf, k);
	if (px >= cx) {
	  it->caret = k;
	  break;
	}
      }
      XwRedraw(p);
      return;
    }

    p->result = p->items[hit].id;
    if (!p->modal && p->activate) {
      XwRedraw(p);
      p->activate(p->result);
    } else {
      p->done = True;
      XwRedraw(p);
    }
    return;

  case KeyPress:
    XwKey(p, &ev->xkey);
    if (!p->done)
      XwRedraw(p);
    return;
  }
}


/*
 * Window handling.
 */

void
XwBuildWindow(XwPanel *p, const char *name, const char *title, int w, int h,
	      Bool modal)
{
  int scr = DefaultScreen(dpy);

  p->w = w;
  p->h = h;
  p->modal = modal;

  p->shell = XtVaCreatePopupShell(name, topLevelShellWidgetClass, toplevel,
				  XtNvisual, DefaultVisual(dpy, scr),
				  XtNdepth, DefaultDepth(dpy, scr),
				  XtNcolormap, DefaultColormap(dpy, scr),
				  XtNtitle, title,
				  XtNiconName, title,
				  XtNwidth, w,
				  XtNheight, h,
				  XtNminWidth, w,
				  XtNminHeight, h,
				  XtNmaxWidth, w,
				  XtNmaxHeight, h,
				  XtNinput, True,
				  NULL);

  p->canvas = XtVaCreateManagedWidget("panel", coreWidgetClass, p->shell,
				      XtNwidth, w,
				      XtNheight, h,
				      XtNbackground, xwBg,
				      XtNborderWidth, 0,
				      NULL);

  XtAddEventHandler(p->canvas,
		    ExposureMask | ButtonPressMask | KeyPressMask,
		    False, XwEvent, (XtPointer)p);

  XtRealizeWidget(p->shell);
  p->win = XtWindow(p->canvas);
  p->gc = XCreateGC(dpy, p->win, 0, NULL);
  XSetFont(dpy, p->gc, xwFont->fid);
  p->buf = XCreatePixmap(dpy, p->win, w, h, DefaultDepth(dpy, scr));
}

void
XwPlaceCentred(XwPanel *p)
{
  XwPlaceAt(p,
	    (WidthOfScreen(XtScreen(p->shell)) - p->w) / 2,
	    (HeightOfScreen(XtScreen(p->shell)) - p->h) / 3);
}

void
XwPlaceAt(XwPanel *p, int x, int y)
{
  int sw = WidthOfScreen(XtScreen(p->shell));
  int sh = HeightOfScreen(XtScreen(p->shell));

  if (x + p->w > sw)
    x = sw - p->w;
  if (y + p->h > sh)
    y = sh - p->h;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  XtVaSetValues(p->shell, XtNx, x, XtNy, y, NULL);
}

void
XwPopup(XwPanel *p)
{
  XtPopup(p->shell, XtGrabNone);
  XSetWMProtocols(dpy, XtWindow(p->shell), &wmDeleteWindow, 1);
  XwRedraw(p);
}

void
XwPopdown(XwPanel *p)
{
  XtPopdown(p->shell);
}

void
XwDestroy(XwPanel *p)
{
  if (p->buf) {
    XFreePixmap(dpy, p->buf);
    p->buf = None;
  }
  if (p->gc) {
    XFreeGC(dpy, p->gc);
    p->gc = NULL;
  }
  if (p->shell) {
    XtDestroyWidget(p->shell);
    p->shell = NULL;
  }
  XSync(dpy, False);
}

int
XwRunModal(XwPanel *p)
{
  p->result = 0;
  p->done = False;

  while (!p->done)
    XtAppProcessEvent(appContext, XtIMAll);

  return p->result;
}
