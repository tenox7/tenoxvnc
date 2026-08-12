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
 * scroll.c - scrolling for desktops bigger than the window.
 *
 * This does the job the Athena Viewport widget used to do, with plain Xt:
 * "viewport" is the visible area, the framebuffer-sized "desktop" window is
 * its child and is moved to negative offsets to scroll it, and the
 * scrollbars are drawn with Xlib into the parent "form".  The desktop window
 * stays exactly framebuffer-sized throughout, so the decoders keep blitting
 * with source and destination coordinates equal and never learn any of this
 * is here.
 *
 * Written for OpenVMS, where DECwindows has no Athena widgets at all, and
 * then used everywhere - it is the last thing that tied the viewer to Xaw.
 *
 * Full-screen mode turns the scrollbars off and drives the position through
 * ScrollTo() instead, which is what bump scrolling does.
 */

#include "vncviewer.h"

#define SB_WIDTH 15		/* scrollbar thickness */
#define SB_MIN	 10		/* smallest thumb we will draw */

static int scrollX = 0, scrollY = 0;	/* framebuffer pixel at the top left */
static int clipW, clipH;		/* size of the visible area */
static Bool haveVert = False, haveHoriz = False;
static Bool barsAllowed = True;
static GC sbGC = NULL;
static unsigned long troughPixel, thumbPixel, edgePixel;
static int dragging = 0;		/* 1 vertical, 2 horizontal */

static void ScrollLayout(void);
static void ScrollDrawBars(void);
static void ScrollToPointer(int x, int y);
static void ScrollFormEvent(Widget w, XtPointer ptr, XEvent *ev,
			    Boolean *cont);
static unsigned long ScrollColour(const char *name, unsigned long fallback);
static void ScrollStrips(int *availW, int *availH);


/*
 * ScrollInit is called once the widgets are realized.  "form" and "viewport"
 * have already been created as plain composites by desktop.c.
 */

void
ScrollInit(void)
{
  Screen *scr = XtScreen(form);

  sbGC = XCreateGC(dpy, XtWindow(form), 0, NULL);

  troughPixel = ScrollColour("grey40", BlackPixelOfScreen(scr));
  thumbPixel = ScrollColour("grey75", WhitePixelOfScreen(scr));
  edgePixel = ScrollColour("black", BlackPixelOfScreen(scr));

  XtAddEventHandler(form, ExposureMask | StructureNotifyMask |
		    ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
		    False, ScrollFormEvent, NULL);

  ScrollLayout();
}


/*
 * ScrollResize - the remote framebuffer changed size.
 */

void
ScrollResize(void)
{
  if (!sbGC)
    return;			/* not realized yet */

  ScrollLayout();
  XClearWindow(dpy, XtWindow(form));
  ScrollDrawBars();
}


/*
 * ScrollAllowBars turns the scrollbars off for full-screen mode, where the
 * whole display should go to the desktop and bump scrolling moves it.
 */

void
ScrollAllowBars(Bool on)
{
  if (barsAllowed == on)
    return;

  barsAllowed = on;
  ScrollResize();
}


void
ScrollGetPos(int *x, int *y)
{
  *x = scrollX;
  *y = scrollY;
}

void
ScrollGetVisible(int *w, int *h)
{
  *w = clipW;
  *h = clipH;
}


/*
 * ScrollTo moves the desktop so that the given framebuffer pixel is at the
 * top left of the visible area, clamped to what actually exists.
 */

void
ScrollTo(int x, int y)
{
  int maxX = si.framebufferWidth - clipW;
  int maxY = si.framebufferHeight - clipH;

  if (x > maxX) x = maxX;
  if (y > maxY) y = maxY;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  if (x == scrollX && y == scrollY)
    return;

  scrollX = x;
  scrollY = y;
  XtMoveWidget(desktop, -scrollX, -scrollY);
  ScrollDrawBars();
}


static unsigned long
ScrollColour(const char *name, unsigned long fallback)
{
  XColor screen, exact;

  /* cmap may be a private BGR233 map with every entry already claimed */
  if (XAllocNamedColor(dpy, cmap, name, &screen, &exact))
    return screen.pixel;

  return fallback;
}


/*
 * ScrollStrips returns the area of "form" left for the desktop once the
 * scrollbars have taken their strips off the right and bottom edges.
 */

static void
ScrollStrips(int *availW, int *availH)
{
  Dimension fw, fh;

  XtVaGetValues(form, XtNwidth, &fw, XtNheight, &fh, NULL);

  *availW = (int)fw - (haveVert ? SB_WIDTH : 0);
  *availH = (int)fh - (haveHoriz ? SB_WIDTH : 0);

  if (*availW < 1) *availW = 1;
  if (*availH < 1) *availH = 1;
}


static void
ScrollLayout(void)
{
  Dimension fw, fh;
  int availW, availH, clipX, clipY;

  XtVaGetValues(form, XtNwidth, &fw, XtNheight, &fh, NULL);
  if (fw == 0 || fh == 0)
    return;

  /* A scrollbar on one axis steals space from the other, so the second test
     has to be made against the already reduced size. */

  if (barsAllowed) {
    haveVert = si.framebufferHeight > (int)fh;
    haveHoriz = si.framebufferWidth > (int)fw - (haveVert ? SB_WIDTH : 0);
    if (haveHoriz && !haveVert)
      haveVert = si.framebufferHeight > (int)fh - SB_WIDTH;
  } else {
    haveVert = haveHoriz = False;
  }

  ScrollStrips(&availW, &availH);

  clipW = si.framebufferWidth < availW ? si.framebufferWidth : availW;
  clipH = si.framebufferHeight < availH ? si.framebufferHeight : availH;

  /* centre the desktop in whatever space is left */
  clipX = (availW - clipW) / 2;
  clipY = (availH - clipH) / 2;

  if (scrollX > si.framebufferWidth - clipW)
    scrollX = si.framebufferWidth - clipW;
  if (scrollY > si.framebufferHeight - clipH)
    scrollY = si.framebufferHeight - clipH;
  if (scrollX < 0) scrollX = 0;
  if (scrollY < 0) scrollY = 0;

  XtConfigureWidget(viewport, clipX, clipY, clipW, clipH, 0);
  XtMoveWidget(desktop, -scrollX, -scrollY);
}


static void
ScrollDrawBars(void)
{
  int availW, availH, len, pos, range;

  if (!sbGC || (!haveVert && !haveHoriz))
    return;

  ScrollStrips(&availW, &availH);

  if (haveVert) {
    XSetForeground(dpy, sbGC, troughPixel);
    XFillRectangle(dpy, XtWindow(form), sbGC, availW, 0, SB_WIDTH, availH);

    len = clipH * availH / si.framebufferHeight;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availH) len = availH;
    range = si.framebufferHeight - clipH;
    pos = range > 0 ? (availH - len) * scrollY / range : 0;

    XSetForeground(dpy, sbGC, thumbPixel);
    XFillRectangle(dpy, XtWindow(form), sbGC, availW + 1, pos + 1,
		   SB_WIDTH - 2, len - 2);
    XSetForeground(dpy, sbGC, edgePixel);
    XDrawRectangle(dpy, XtWindow(form), sbGC, availW, pos,
		   SB_WIDTH - 1, len - 1);
  }

  if (haveHoriz) {
    XSetForeground(dpy, sbGC, troughPixel);
    XFillRectangle(dpy, XtWindow(form), sbGC, 0, availH, availW, SB_WIDTH);

    len = clipW * availW / si.framebufferWidth;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availW) len = availW;
    range = si.framebufferWidth - clipW;
    pos = range > 0 ? (availW - len) * scrollX / range : 0;

    XSetForeground(dpy, sbGC, thumbPixel);
    XFillRectangle(dpy, XtWindow(form), sbGC, pos + 1, availH + 1,
		   len - 2, SB_WIDTH - 2);
    XSetForeground(dpy, sbGC, edgePixel);
    XDrawRectangle(dpy, XtWindow(form), sbGC, pos, availH,
		   len - 1, SB_WIDTH - 1);
  }
}


/*
 * ScrollToPointer centres the thumb of whichever scrollbar is being dragged
 * on the pointer and scrolls the desktop to match.
 */

static void
ScrollToPointer(int x, int y)
{
  int availW, availH, len, pos;

  ScrollStrips(&availW, &availH);

  if (dragging == 1 && haveVert) {
    len = clipH * availH / si.framebufferHeight;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availH) len = availH;
    pos = y - len / 2;
    if (pos < 0) pos = 0;
    if (pos > availH - len) pos = availH - len;
    ScrollTo(scrollX, (availH - len) > 0
	     ? (si.framebufferHeight - clipH) * pos / (availH - len) : 0);

  } else if (dragging == 2 && haveHoriz) {
    len = clipW * availW / si.framebufferWidth;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availW) len = availW;
    pos = x - len / 2;
    if (pos < 0) pos = 0;
    if (pos > availW - len) pos = availW - len;
    ScrollTo((availW - len) > 0
	     ? (si.framebufferWidth - clipW) * pos / (availW - len) : 0,
	     scrollY);
  }
}


static void
ScrollFormEvent(Widget w, XtPointer ptr, XEvent *ev, Boolean *cont)
{
  int availW, availH, x, y;

  switch (ev->type) {

  case ConfigureNotify:
    ScrollLayout();
    XClearWindow(dpy, XtWindow(form));
    ScrollDrawBars();
    return;

  case Expose:
    ScrollDrawBars();
    return;

  case ButtonPress:
    ScrollStrips(&availW, &availH);
    x = ev->xbutton.x;
    y = ev->xbutton.y;
    if (haveVert && x >= availW)
      dragging = 1;
    else if (haveHoriz && y >= availH)
      dragging = 2;
    else
      return;
    ScrollToPointer(x, y);
    return;

  case MotionNotify:
    if (dragging)
      ScrollToPointer(ev->xmotion.x, ev->xmotion.y);
    return;

  case ButtonRelease:
    dragging = 0;
    return;
  }
}
