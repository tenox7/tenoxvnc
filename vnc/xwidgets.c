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
 * desktop may be on a private BGR233 map in which these colors would mean
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
XwColor(const char *spec, unsigned long fallback)
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

  xwBg = XwColor("gray85", WhitePixel(dpy, scr));
  xwFg = XwColor("black", BlackPixel(dpy, scr));
  xwField = XwColor("white", WhitePixel(dpy, scr));
  xwDark = XwColor("gray45", BlackPixel(dpy, scr));
  xwLight = XwColor("gray100", WhitePixel(dpy, scr));
  xwWarn = XwColor("red3", BlackPixel(dpy, scr));

  return True;
}


int
XwStrW(const char *s)
{
  return XTextWidth(xwFont, s, strlen(s));
}

/* what a checkbox needs: the box, a gap, then the label.  A radio button is
   the same size, so it uses this too. */
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

/* A radio button stands for one value of a setting.  The buttons of a group
   are told apart only by sharing a value pointer, so nothing has to hold the
   group together: each draws itself filled in when the setting happens to be
   its own value, and clicking one simply stores that value. */

XwItem *
XwAddRadio(XwPanel *p, const char *s, int *value, int val, int x, int y)
{
  XwItem *it = XwAdd(p, XW_RADIO, s, x, y, XwCheckW(s), xwLineH);

  it->num = value;
  it->val = val;
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

/*
 * A slider picks one of a fixed list of values, which is what the numeric
 * options actually are: depth has to be a depth the X server really has, and
 * the two levels run 0 to 9.  The first notch may be the unset one - depth 0
 * or compression level -1 - and is drawn as "auto", since a blank field says
 * nothing about what the viewer will do when it is left alone.
 *
 * The value itself is drawn at the right end and the room for it is kept
 * back from the track, so the thumb has the same travel whatever is showing.
 */

/* the groove sits under the top of the thumb, the notches below both */
#define XW_SLIDER_GROOVE	5
#define XW_SLIDER_THUMB		(xwLineH - 2)

static int
XwThumbW(void)
{
  return xwCharW + 2;
}

static void
XwSliderText(XwItem *it, char *out, int val)
{
  if (it->autoFirst && val == it->vals[0])
    strcpy(out, "auto");
  else
    sprintf(out, "%d", val);
}

/* left edge of the thumb when it sits on notch i */
static int
XwSliderX(XwItem *it, int i)
{
  int travel = it->w - it->valW - xwCharW - XwThumbW();

  if (it->nvals < 2 || travel < 1)
    return it->x;

  return it->x + i * travel / (it->nvals - 1);
}

/* which notch the value is on; one we do not offer reads as the first */
static int
XwSliderIndex(XwItem *it)
{
  int i;

  for (i = 0; i < it->nvals; i++)
    if (it->vals[i] == *it->num)
      return i;

  return 0;
}

XwItem *
XwAddSlider(XwPanel *p, int *value, const int *vals, int nvals, Bool autoFirst,
	    int x, int y, int w)
{
  XwItem *it = XwAdd(p, XW_SLIDER, NULL, x, y, w, xwLineH + 6);
  char text[24];
  int i;

  it->num = value;
  it->vals = vals;
  it->nvals = nvals;
  it->autoFirst = autoFirst;

  for (i = 0; i < nvals; i++) {
    XwSliderText(it, text, vals[i]);
    if (XwStrW(text) > it->valW)
      it->valW = XwStrW(text);
  }

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
 * Whether an item can be used at all.  disabled is settled when the panel is
 * built; enableIf is looked at every time, so a control that only means
 * something while a checkbox is ticked greys itself out the moment it is not.
 */

Bool
XwLive(XwItem *it)
{
  return !it->disabled && (!it->enableIf || *it->enableIf);
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
  int tx, ty, n, boxY, box, k, trackW, groove;
  const char *s;
  char stars[64], slider[24];

  ty = it->y + xwFont->ascent;

  switch (it->kind) {

  case XW_LABEL:
    XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
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
      XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
      XDrawLine(dpy, p->buf, p->gc, it->x + 3, boxY + box / 2,
		it->x + box / 2 - 1, boxY + box - 4);
      XDrawLine(dpy, p->buf, p->gc, it->x + 3, boxY + box / 2 - 1,
		it->x + box / 2 - 1, boxY + box - 5);
      XDrawLine(dpy, p->buf, p->gc, it->x + box / 2 - 1, boxY + box - 4,
		it->x + box - 3, boxY + 3);
      XDrawLine(dpy, p->buf, p->gc, it->x + box / 2 - 1, boxY + box - 5,
		it->x + box - 3, boxY + 2);
    }

    XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
    XDrawString(dpy, p->buf, p->gc, it->x + box + 6, ty,
		it->label, strlen(it->label));

    if (i == p->focus)
      XDrawRectangle(dpy, p->buf, p->gc, it->x - 2, it->y - 1,
		     it->w + 3, it->h + 1);
    break;

  case XW_RADIO:
    /* Round, so it cannot be mistaken for a checkbox: one of these is always
       on, where a checkbox stands on its own. */
    box = xwLineH - 4;
    boxY = it->y + 2;
    XSetForeground(dpy, p->gc, xwField);
    XFillArc(dpy, p->buf, p->gc, it->x, boxY, box, box, 0, 360 * 64);
    XSetForeground(dpy, p->gc, xwDark);
    XDrawArc(dpy, p->buf, p->gc, it->x, boxY, box, box, 45 * 64, 180 * 64);
    XSetForeground(dpy, p->gc, xwLight);
    XDrawArc(dpy, p->buf, p->gc, it->x, boxY, box, box, 225 * 64, 180 * 64);

    XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
    if (*it->num == it->val) {
      n = box / 2 - 1;
      XFillArc(dpy, p->buf, p->gc, it->x + (box - n + 1) / 2,
	       boxY + (box - n + 1) / 2, n, n, 0, 360 * 64);
    }

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
    XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
    tx = it->x + (it->w - XwStrW(it->label)) / 2;
    XDrawString(dpy, p->buf, p->gc, tx, it->y + 3 + xwFont->ascent,
		it->label, strlen(it->label));
    if (i == p->focus)
      XDrawRectangle(dpy, p->buf, p->gc, it->x + 2, it->y + 2,
		     it->w - 5, it->h - 5);
    break;

  case XW_SLIDER:
    /* groove across the top, the thumb riding over it, and the notches
       marked underneath where the thumb cannot cover them */
    trackW = it->w - it->valW - xwCharW;
    groove = it->y + XW_SLIDER_GROOVE;

    /* an empty groove is the clearest sign the slider is not in play */
    XSetForeground(dpy, p->gc, XwLive(it) ? xwField : xwBg);
    XFillRectangle(dpy, p->buf, p->gc, it->x, groove, trackW, 4);
    XwFrame(p, it->x, groove, trackW, 4, True);

    XSetForeground(dpy, p->gc, xwDark);
    for (k = 0; k < it->nvals; k++) {
      tx = XwSliderX(it, k) + XwThumbW() / 2;
      XDrawLine(dpy, p->buf, p->gc, tx, it->y + XW_SLIDER_THUMB + 2,
		tx, it->y + XW_SLIDER_THUMB + 4);
    }

    tx = XwSliderX(it, XwSliderIndex(it));
    XSetForeground(dpy, p->gc, xwBg);
    XFillRectangle(dpy, p->buf, p->gc, tx, it->y, XwThumbW(), XW_SLIDER_THUMB);
    XwFrame(p, tx, it->y, XwThumbW(), XW_SLIDER_THUMB, False);
    if (i == p->focus)
      XDrawRectangle(dpy, p->buf, p->gc, tx + 2, it->y + 2,
		     XwThumbW() - 5, XW_SLIDER_THUMB - 5);

    XwSliderText(it, slider, *it->num);
    XSetForeground(dpy, p->gc, XwLive(it) ? xwFg : xwDark);
    XDrawString(dpy, p->buf, p->gc, it->x + it->w - XwStrW(slider),
		groove + 2 + xwFont->ascent / 2, slider, strlen(slider));
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

  return XwLive(&p->items[i]) &&
	 (k == XW_TEXT || k == XW_CHECK || k == XW_RADIO ||
	  k == XW_BUTTON || k == XW_SLIDER);
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
 * An option control has no id: the panel owns it, so it acts in place and
 * never finishes the dialog.  One with an id is a menu entry, and its owner
 * does the work in the activate callback.
 */

static Bool
XwPanelAction(XwPanel *p, int i)
{
  XwItem *it = &p->items[i];

  if (it->id)
    return False;

  if (it->kind == XW_RADIO) {
    *it->num = it->val;
    XwRedraw(p);
    return True;
  }

  if (it->kind != XW_CHECK)
    return False;

  it->state = !(it->flag ? *it->flag : it->state);
  if (it->flag)
    *it->flag = it->state;

  XwRedraw(p);
  return True;
}


/*
 * Sliders: set from a pointer position, or step a notch at a time from the
 * keyboard.  Both are panel business, so neither finishes the dialog.
 */

static void
XwSliderSet(XwPanel *p, XwItem *it, int x)
{
  int travel = it->w - it->valW - xwCharW - XwThumbW();
  int i;

  if (it->nvals < 2 || travel < 1)
    return;

  x -= it->x + XwThumbW() / 2;
  if (x < 0)
    x = 0;

  i = (x * (it->nvals - 1) + travel / 2) / travel;
  if (i >= it->nvals)
    i = it->nvals - 1;

  if (*it->num == it->vals[i])
    return;

  *it->num = it->vals[i];
  XwRedraw(p);
}

static void
XwSliderStep(XwPanel *p, XwItem *it, int dir)
{
  int i = XwSliderIndex(it) + dir;

  if (i < 0 || i >= it->nvals)
    return;

  *it->num = it->vals[i];
  XwRedraw(p);
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

  /* what had the focus may have been greyed out since, by a checkbox it
     hangs off being unticked */
  if (it && !XwLive(it))
    it = NULL;

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
    if (it && (it->kind == XW_BUTTON || (it->kind == XW_CHECK && it->id)))
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

  case XK_Left:
  case XK_Right:
    if (it && it->kind == XW_SLIDER && !it->id) {
      XwSliderStep(p, it, ks == XK_Right ? 1 : -1);
      return;
    }
    break;			/* in a text field an arrow moves the caret */

  case XK_space:
    if (it && (it->kind == XW_CHECK || it->kind == XW_RADIO ||
	       it->kind == XW_BUTTON)) {
      if (XwPanelAction(p, p->focus))
	return;
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
  /* the XK_KP_Home..XK_KP_Delete block postdates X11R4 (OpenWindows 3) */
#ifdef XK_KP_Delete
  case XK_KP_Delete:
#endif
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

    if (p->items[hit].kind == XW_SLIDER && !p->items[hit].id) {
      p->drag = hit;
      XwSliderSet(p, &p->items[hit], ev->xbutton.x);
      XwRedraw(p);		/* the focus ring moved even if the value did not */
      return;
    }

    if (XwPanelAction(p, hit))
      return;

    p->result = p->items[hit].id;
    if (!p->modal && p->activate) {
      XwRedraw(p);
      p->activate(p->result);
    } else {
      p->done = True;
      XwRedraw(p);
    }
    return;

  case MotionNotify:
    if (p->drag >= 0)
      XwSliderSet(p, &p->items[p->drag], ev->xmotion.x);
    return;

  case ButtonRelease:
    p->drag = -1;
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

  p->drag = -1;

  XtAddEventHandler(p->canvas,
		    ExposureMask | ButtonPressMask | ButtonReleaseMask |
		    Button1MotionMask | KeyPressMask,
		    False, XwEvent, (XtPointer)p);

  /* The window manager gives the focus to the shell, not to the canvas inside
     it.  A key event goes to the deepest window under the pointer only while
     the pointer is somewhere in the focus window's tree; with the pointer off
     the panel the shell itself is the source, and events propagate upwards,
     so the canvas would never see it.  Listening on both means the panel takes
     what is typed wherever the pointer happens to be sitting, and nothing
     arrives twice: propagation stops at the first window that asked for it. */
  XtAddEventHandler(p->shell, KeyPressMask, False, XwEvent, (XtPointer)p);

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
