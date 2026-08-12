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
 * vms.c - OpenVMS support code.
 *
 * Scrolling.  DECwindows has no Athena widget set, so there is no Viewport
 * widget to put the desktop in.  This does the same job with plain Xt: the
 * "viewport" composite is the visible area, the framebuffer-sized "desktop"
 * window is its child and is moved to negative offsets to scroll it, and
 * the scrollbars are drawn with Xlib into the parent "form" composite.  The
 * desktop window stays exactly framebuffer-sized, so the decoders keep
 * blitting with source and destination coordinates equal and never learn
 * that any of this is here.
 *
 * Plus three CRTL replacements: a socket poll for the missing usleep() and
 * for select() with no descriptors, ioctl() for the fcntl() that cannot set
 * O_NONBLOCK on a VMS socket, and a terminal prompt for the absent
 * getpass().
 */

#include <vncviewer.h>
#include <sys/ioctl.h>
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <starlet.h>

#define SB_WIDTH 15		/* scrollbar thickness */
#define SB_MIN	 10		/* smallest thumb we will draw */

static int scrollX = 0, scrollY = 0;	/* framebuffer pixel at the top left */
static int clipW, clipH;		/* size of the visible area */
static Bool haveVert = False, haveHoriz = False;
static GC sbGC = NULL;
static unsigned long troughPixel, thumbPixel, edgePixel;
static int dragging = 0;		/* 1 vertical, 2 horizontal */

static void VmsScrollLayout(void);
static void VmsDrawScrollbars(void);
static void VmsScrollToPointer(int x, int y);
static void VmsFormEvent(Widget w, XtPointer ptr, XEvent *ev, Boolean *cont);
static unsigned long VmsColour(const char *name, unsigned long fallback);
static void VmsStrips(int *availW, int *availH);


/*
 * VmsScrollInit is called once the widgets are realized.  "form" and
 * "viewport" have already been created as plain composites by desktop.c.
 */

void
VmsScrollInit(void)
{
  Screen *scr = XtScreen(form);

  sbGC = XCreateGC(dpy, XtWindow(form), 0, NULL);

  troughPixel = VmsColour("grey40", BlackPixelOfScreen(scr));
  thumbPixel = VmsColour("grey75", WhitePixelOfScreen(scr));
  edgePixel = VmsColour("black", BlackPixelOfScreen(scr));

  XtAddEventHandler(form, ExposureMask | StructureNotifyMask |
		    ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
		    False, VmsFormEvent, NULL);

  VmsScrollLayout();
}


/*
 * VmsScrollResize - the remote framebuffer changed size.
 */

void
VmsScrollResize(void)
{
  if (!sbGC)
    return;			/* not realized yet */

  VmsScrollLayout();
  XClearWindow(dpy, XtWindow(form));
  VmsDrawScrollbars();
}


static unsigned long
VmsColour(const char *name, unsigned long fallback)
{
  XColor screen, exact;

  /* cmap may be a private BGR233 map with every entry already claimed */
  if (XAllocNamedColor(dpy, cmap, name, &screen, &exact))
    return screen.pixel;

  return fallback;
}


/*
 * VmsStrips returns the area of "form" left over for the desktop once the
 * scrollbars have taken their strips off the right and bottom edges.
 */

static void
VmsStrips(int *availW, int *availH)
{
  Dimension fw, fh;

  XtVaGetValues(form, XtNwidth, &fw, XtNheight, &fh, NULL);

  *availW = (int)fw - (haveVert ? SB_WIDTH : 0);
  *availH = (int)fh - (haveHoriz ? SB_WIDTH : 0);

  if (*availW < 1) *availW = 1;
  if (*availH < 1) *availH = 1;
}


/*
 * VmsScrollLayout decides which scrollbars are needed, sizes the visible
 * area and positions the desktop window inside it.
 */

static void
VmsScrollLayout(void)
{
  Dimension fw, fh;
  int availW, availH, maxX, maxY, clipX, clipY;

  XtVaGetValues(form, XtNwidth, &fw, XtNheight, &fh, NULL);
  if (fw == 0 || fh == 0)
    return;

  /* A scrollbar on one axis steals space from the other, so the second
     test has to be made against the already reduced size. */

  haveVert = si.framebufferHeight > (int)fh;
  haveHoriz = si.framebufferWidth > (int)fw - (haveVert ? SB_WIDTH : 0);
  if (haveHoriz && !haveVert)
    haveVert = si.framebufferHeight > (int)fh - SB_WIDTH;

  VmsStrips(&availW, &availH);

  clipW = si.framebufferWidth < availW ? si.framebufferWidth : availW;
  clipH = si.framebufferHeight < availH ? si.framebufferHeight : availH;

  /* centre the desktop in whatever space is left, as the Athena build does */
  clipX = (availW - clipW) / 2;
  clipY = (availH - clipH) / 2;

  maxX = si.framebufferWidth - clipW;
  maxY = si.framebufferHeight - clipH;
  if (scrollX > maxX) scrollX = maxX;
  if (scrollY > maxY) scrollY = maxY;
  if (scrollX < 0) scrollX = 0;
  if (scrollY < 0) scrollY = 0;

  XtConfigureWidget(viewport, clipX, clipY, clipW, clipH, 0);
  XtMoveWidget(desktop, -scrollX, -scrollY);
}


static void
VmsDrawScrollbars(void)
{
  int availW, availH, len, pos, range;

  if (!sbGC || (!haveVert && !haveHoriz))
    return;

  VmsStrips(&availW, &availH);

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
 * VmsScrollToPointer centres the thumb of whichever scrollbar is being
 * dragged on the pointer and scrolls the desktop to match.
 */

static void
VmsScrollToPointer(int x, int y)
{
  int availW, availH, len, pos, oldX = scrollX, oldY = scrollY;

  VmsStrips(&availW, &availH);

  if (dragging == 1 && haveVert) {
    len = clipH * availH / si.framebufferHeight;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availH) len = availH;
    pos = y - len / 2;
    if (pos < 0) pos = 0;
    if (pos > availH - len) pos = availH - len;
    scrollY = (availH - len) > 0
	      ? (si.framebufferHeight - clipH) * pos / (availH - len) : 0;

  } else if (dragging == 2 && haveHoriz) {
    len = clipW * availW / si.framebufferWidth;
    if (len < SB_MIN) len = SB_MIN;
    if (len > availW) len = availW;
    pos = x - len / 2;
    if (pos < 0) pos = 0;
    if (pos > availW - len) pos = availW - len;
    scrollX = (availW - len) > 0
	      ? (si.framebufferWidth - clipW) * pos / (availW - len) : 0;
  }

  if (scrollX == oldX && scrollY == oldY)
    return;

  XtMoveWidget(desktop, -scrollX, -scrollY);
  VmsDrawScrollbars();
}


static void
VmsFormEvent(Widget w, XtPointer ptr, XEvent *ev, Boolean *cont)
{
  int availW, availH, x, y;

  switch (ev->type) {

  case ConfigureNotify:
    VmsScrollLayout();
    XClearWindow(dpy, XtWindow(form));
    VmsDrawScrollbars();
    return;

  case Expose:
    VmsDrawScrollbars();
    return;

  case ButtonPress:
    VmsStrips(&availW, &availH);
    x = ev->xbutton.x;
    y = ev->xbutton.y;
    if (haveVert && x >= availW)
      dragging = 1;
    else if (haveHoriz && y >= availH)
      dragging = 2;
    else
      return;
    VmsScrollToPointer(x, y);
    return;

  case MotionNotify:
    if (dragging)
      VmsScrollToPointer(ev->xmotion.x, ev->xmotion.y);
    return;

  case ButtonRelease:
    dragging = 0;
    return;
  }
}


/*
 * VmsSocketReady waits up to msec milliseconds for the socket to become
 * readable.  select() on VMS works for sockets but not for a bare timeout
 * with no descriptors, which is what Msleep() would want.
 */

Bool
VmsSocketReady(int sock, int msec)
{
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  tv.tv_sec = msec / 1000;
  tv.tv_usec = (msec % 1000) * 1000;

  return select(sock + 1, &fds, NULL, NULL, &tv) > 0;
}


/*
 * VmsPrompt reads a line from the terminal, optionally without echoing it.
 * Replaces getpass(), which the VAX CRTL does not have, and the Athena
 * dialog boxes.  Returns a pointer to a static, writable buffer - callers
 * truncate and wipe it in place.
 */

char *
VmsPrompt(const char *prompt, Bool echo)
{
  static char buf[256];
  $DESCRIPTOR(devDsc, "SYS$COMMAND");
  struct {
    unsigned short status;
    unsigned short count;
    unsigned int info;
  } iosb;
  unsigned short chan;
  int status, len;

  buf[0] = '\0';

  /* A no-echo read needs the terminal driver; if SYS$COMMAND is not a
     terminal (a detached process, or a pipe) fall back to stdio. */

  if (!echo) {
    status = sys$assign(&devDsc, &chan, 0, 0);
    if (status & 1) {
      status = sys$qiow(0, chan, IO$_READPROMPT | IO$M_NOECHO | IO$M_PURGE,
			&iosb, 0, 0, buf, sizeof(buf) - 1, 0, 0,
			(char *)prompt, strlen(prompt));
      sys$dassgn(chan);

      if ((status & 1) && (iosb.status & 1)) {
	buf[iosb.count] = '\0';
	fprintf(stderr, "\n");
	return buf;
      }
    }
  }

  fprintf(stderr, "%s", prompt);
  fflush(stderr);

  if (!fgets(buf, sizeof(buf), stdin))
    return NULL;

  len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    buf[--len] = '\0';

  return buf;
}
