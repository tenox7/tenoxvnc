/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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
 * desktop.c - functions to deal with "desktop" window.
 */

#include <vncviewer.h>
#include <X11/cursorfont.h>
#ifdef MITSHM
#include <X11/extensions/XShm.h>
#endif

GC gc;
GC srcGC, dstGC; /* used for debugging copyrect */
Window desktopWin;
Cursor dotCursor;
Widget form, viewport, desktop;

static Bool modifierPressed[256];

static XImage *image = NULL;
static Bool imageIsShm = False;

static XtIntervalId resizeTimer = 0;
static int wantResizeWidth, wantResizeHeight;

enum { CURSOR_DOT, CURSOR_ARROW, CURSOR_NONE };
static const char *cursorModeNames[] = { "dot", "arrow", "none" };
static int cursorMode = CURSOR_DOT;

static Cursor CreateDotCursor();
static Cursor CursorForMode(void);
static void CreateDesktopImage(void);
static void BlitToImage(XImage *img, char *buf, int x, int y, int width,
			int height);
static void CopyBGR233ToImage(XImage *img, CARD8 *buf, int x, int y,
			      int width, int height);
static void HandleBasicDesktopEvent(Widget w, XtPointer ptr, XEvent *ev,
				    Boolean *cont);
static void HandleToplevelConfigure(Widget w, XtPointer ptr, XEvent *ev,
				    Boolean *cont);
static void ResizeTimerCallback(XtPointer clientData, XtIntervalId *id);
static void ScheduleRemoteResize(int width, int height);

/*
 * The backingStore resource and the string converter for it.
 */

#define XtNbackingStore "backingStore"
#define XtCBackingStore "BackingStore"
#define XtRBackingStore "BackingStore"

static void
CvtStringToBackingStore(XrmValue *args, Cardinal *num_args, XrmValue *from,
			XrmValue *to)
{
  static int result;
  char *s = (char *)from->addr;

  if (strcasecmp(s, "notUseful") == 0)
    result = NotUseful;
  else if (strcasecmp(s, "whenMapped") == 0)
    result = WhenMapped;
  else if (strcasecmp(s, "always") == 0)
    result = Always;
  else if (strcasecmp(s, "default") == 0)
    result = Always;
  else {
    XtStringConversionWarning(s, XtRBackingStore);
    result = Always;
  }

  /* not XPointer: X11R4 (OpenWindows 3) has no such typedef, addr is caddr_t */
  to->addr = (char *)&result;
  to->size = sizeof(result);
}

static XtResource desktopBackingStoreResources[] = {
  {
    XtNbackingStore, XtCBackingStore, XtRBackingStore, sizeof(int), 0,
    XtRImmediate, (XtPointer) Always,
  },
};


/*
 * DesktopInitBeforeRealization creates the "desktop" widget and the viewport
 * which controls it.
 */

void
DesktopInitBeforeRealization()
{
  int i;

  /* "form" is the background and carries the scrollbars, "viewport" is the
     visible area that clips "desktop".  scroll.c does the geometry.

     All three need a size up front - Xt refuses to realize a widget of zero
     width or height, and ScrollInit() cannot lay them out until the windows
     exist.  It corrects all of this straight after realization. */

  form = XtVaCreateManagedWidget("form", compositeWidgetClass, toplevel,
				 XtNborderWidth, 0,
				 XtNwidth, si.framebufferWidth,
				 XtNheight, si.framebufferHeight, NULL);

  viewport = XtVaCreateManagedWidget("viewport", compositeWidgetClass, form,
				     XtNborderWidth, 0,
				     XtNwidth, si.framebufferWidth,
				     XtNheight, si.framebufferHeight, NULL);

  desktop = XtVaCreateManagedWidget("desktop", coreWidgetClass, viewport,
				    XtNborderWidth, 0,
				    XtNwidth, si.framebufferWidth,
				    XtNheight, si.framebufferHeight, NULL);

  XtAddEventHandler(desktop, LeaveWindowMask|ExposureMask,
		    True, HandleBasicDesktopEvent, NULL);

  for (i = 0; i < 256; i++)
    modifierPressed[i] = False;

  CreateDesktopImage();
}


/*
 * CreateDesktopImage allocates the local framebuffer image at the current
 * si.framebufferWidth/Height, using MIT-SHM when possible.
 */

static void
CreateDesktopImage(void)
{
  image = NULL;
  imageIsShm = False;

#ifdef MITSHM
  if (appData.useShm) {
    image = CreateShmImage();
    if (!image)
      appData.useShm = False;
    else
      imageIsShm = True;
  }
#endif

  if (!image) {
    image = XCreateImage(dpy, vis, visdepth, ZPixmap, 0, NULL,
			 si.framebufferWidth, si.framebufferHeight,
			 BitmapPad(dpy), 0);

    image->data = malloc(image->bytes_per_line * image->height);
    if (!image->data) {
      fprintf(stderr,"malloc failed\n");
      exit(1);
    }
  }

  memset(image->data, 0, image->bytes_per_line * image->height);
}


/*
 * DesktopInitAfterRealization does things which require the X windows to
 * exist.  It creates some GCs and sets the dot cursor.
 */

void
DesktopInitAfterRealization()
{
  XGCValues gcv;
  XSetWindowAttributes attr;
  unsigned long valuemask;
  int i;

  desktopWin = XtWindow(desktop);

  gc = XCreateGC(dpy,desktopWin,0,NULL);

  gcv.function = GXxor;
  gcv.foreground = 0x0f0f0f0f;
  srcGC = XCreateGC(dpy,desktopWin,GCFunction|GCForeground,&gcv);
  gcv.foreground = 0xf0f0f0f0;
  dstGC = XCreateGC(dpy,desktopWin,GCFunction|GCForeground,&gcv);

  XtAddConverter(XtRString, XtRBackingStore, CvtStringToBackingStore,
		 NULL, 0);

  XtVaGetApplicationResources(desktop, (XtPointer)&attr.backing_store,
			      desktopBackingStoreResources, 1, NULL);
  valuemask = CWBackingStore;

  for (i = 0; i < 3; i++)
    if (strcasecmp(appData.localCursor, cursorModeNames[i]) == 0)
      cursorMode = i;

  if (!appData.useX11Cursor) {
    attr.cursor = CursorForMode();
    valuemask |= CWCursor;
  }

  XChangeWindowAttributes(dpy, desktopWin, valuemask, &attr);

  ScrollInit();			/* needs the windows to exist */
}


/*
 * ResizeDesktopFramebuffer - the remote framebuffer changed size (NewFBSize
 * or ExtendedDesktopSize).  Reallocate the local image and resize the
 * widgets to match.
 */

void
ResizeDesktopFramebuffer(int width, int height)
{
  if (width == si.framebufferWidth && height == si.framebufferHeight)
    return;

  fprintf(stderr, "Framebuffer size changed to %dx%d\n", width, height);

#ifdef VNCSTATS
  {
    char note[48];

    sprintf(note, "framebuffer resized to %dx%d", width, height);
    StatsLog(-1, note, 0.0, 0.0);
  }
#endif

  /* hide the soft cursor while the ground shifts under it */
  SoftCursorLockArea(0, 0, si.framebufferWidth, si.framebufferHeight);

#ifdef MITSHM
  if (imageIsShm) {
    ShmDetachImage(image);
    image = NULL;
  }
#endif
  if (image) {
    XDestroyImage(image);	/* also frees the malloc'd data */
    image = NULL;
  }

  si.framebufferWidth = width;
  si.framebufferHeight = height;

  CreateDesktopImage();

  /* Raise the max size hints first so the window may grow, then resize the
     desktop widget and the toplevel to match the new framebuffer. */

  if (supportsSetDesktopSize && appData.useRemoteResize) {
    XtVaSetValues(toplevel, XtNmaxWidth, 32767, XtNmaxHeight, 32767, NULL);
  } else {
    XtVaSetValues(toplevel, XtNmaxWidth, width, XtNmaxHeight, height, NULL);
  }

  /* a bare composite has no geometry manager, so resize from the parent */
  XtResizeWidget(desktop, width, height, 0);
  ScrollResize();

  if (!appData.fullScreen) {
    Dimension w = width, h = height;

    if (w + appData.wmDecorationWidth >= dpyWidth)
      w = dpyWidth - appData.wmDecorationWidth;
    if (h + appData.wmDecorationHeight >= dpyHeight)
      h = dpyHeight - appData.wmDecorationHeight;

    XtVaSetValues(toplevel, XtNwidth, w, XtNheight, h, NULL);
  }

  SoftCursorUnlockScreen();
}


/*
 * Remote resize plumbing: when the user resizes our window and the server
 * accepts SetDesktopSize, ask the server to resize its framebuffer to
 * match.  Requests are debounced and only one is in flight at a time.
 */

static void
ScheduleRemoteResize(int width, int height)
{
  wantResizeWidth = width;
  wantResizeHeight = height;

  if (!supportsSetDesktopSize || !appData.useRemoteResize || appData.viewOnly)
    return;

  if (width == si.framebufferWidth && height == si.framebufferHeight)
    return;

  if (resizeTimer)
    XtRemoveTimeOut(resizeTimer);
  resizeTimer = XtAppAddTimeOut(appContext, 250, ResizeTimerCallback, NULL);
}

static void
ResizeTimerCallback(XtPointer clientData, XtIntervalId *id)
{
  resizeTimer = 0;

  if (!supportsSetDesktopSize || !appData.useRemoteResize || appData.viewOnly)
    return;

  if (pendingDesktopResize) {
    /* a request is still in flight - try again shortly */
    resizeTimer = XtAppAddTimeOut(appContext, 100, ResizeTimerCallback, NULL);
    return;
  }

  if (wantResizeWidth == si.framebufferWidth &&
      wantResizeHeight == si.framebufferHeight)
    return;

  pendingDesktopResize = True;
  SendSetDesktopSize(wantResizeWidth, wantResizeHeight);
}

static void
HandleToplevelConfigure(Widget w, XtPointer ptr, XEvent *ev, Boolean *cont)
{
  if (ev->type != ConfigureNotify)
    return;

  /* Full-screen resizes the window to the local screen; that is our own
     doing, not the user asking for a different desktop size. */
  if (appData.fullScreen)
    return;

  ScheduleRemoteResize(ev->xconfigure.width, ev->xconfigure.height);
}

void
TrackDesktopResizes(void)
{
  XtAddEventHandler(toplevel, StructureNotifyMask, False,
		    HandleToplevelConfigure, NULL);
}


/*
 * DesktopSizeSupportLearned is called when the first ExtendedDesktopSize
 * rect arrives.  Lift the max size limits on the window, and if the window
 * size differs from the framebuffer (window clamped to a small screen, or
 * an explicit -geometry), bring the remote desktop in line with it.
 */

void
DesktopSizeSupportLearned(void)
{
  Dimension w, h;

  if (!appData.useRemoteResize || appData.viewOnly)
    return;

  XtVaSetValues(toplevel, XtNmaxWidth, 32767, XtNmaxHeight, 32767, NULL);

  /* The window is the size of the screen while full-screen, which says
     nothing about what the desktop should be. */
  if (appData.fullScreen)
    return;

  XtVaGetValues(toplevel, XtNwidth, &w, XtNheight, &h, NULL);
  ScheduleRemoteResize(w, h);
}


/*
 * HandleBasicDesktopEvent - deal with expose and leave events.
 */

static void
HandleBasicDesktopEvent(Widget w, XtPointer ptr, XEvent *ev, Boolean *cont)
{
  int i;

  switch (ev->type) {

  case Expose:
  case GraphicsExpose:
    /* sometimes due to scrollbars being added/removed we get an expose outside
       the actual desktop area.  Make sure we don't pass it on to the RFB
       server. */

    if (ev->xexpose.x + ev->xexpose.width > si.framebufferWidth) {
      ev->xexpose.width = si.framebufferWidth - ev->xexpose.x;
      if (ev->xexpose.width <= 0) break;
    }

    if (ev->xexpose.y + ev->xexpose.height > si.framebufferHeight) {
      ev->xexpose.height = si.framebufferHeight - ev->xexpose.y;
      if (ev->xexpose.height <= 0) break;
    }

    SendFramebufferUpdateRequest(ev->xexpose.x, ev->xexpose.y,
				 ev->xexpose.width, ev->xexpose.height, False);
    break;

  case LeaveNotify:
    for (i = 0; i < 256; i++) {
      if (modifierPressed[i]) {
	SendKeyEvent(XKeycodeToKeysym(dpy, i, 0), False);
	modifierPressed[i] = False;
      }
    }
    break;
  }
}


/*
 * SendRFBEvent is an action which sends an RFB event.  It can be used in two
 * ways.  Without any parameters it simply sends an RFB event corresponding to
 * the X event which caused it to be called.  With parameters, it generates a
 * "fake" RFB event based on those parameters.  The first parameter is the
 * event type, either "fbupdate", "ptr", "keydown", "keyup" or "key"
 * (down&up).  The "fbupdate" event requests full framebuffer update. For a
 * "key" event the second parameter is simply a keysym string as understood by
 * XStringToKeysym().  For a "ptr" event, the following three parameters are
 * just X, Y and the button mask (0 for all up, 1 for button1 down, 2 for
 * button2 down, 3 for both, etc).
 */

void
SendRFBEvent(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  KeySym ks;
  char keyname[256];
  int buttonMask, x, y;

  if (appData.fullScreen && ev->type == MotionNotify) {
    if (BumpScroll(ev))
      return;
  }

  if (appData.viewOnly) return;

  if (*num_params != 0) {
    if (strncasecmp(params[0],"key",3) == 0) {
      if (*num_params != 2) {
	fprintf(stderr,
		"Invalid params: SendRFBEvent(key|keydown|keyup,<keysym>)\n");
	return;
      }
      ks = XStringToKeysym(params[1]);
      if (ks == NoSymbol) {
	fprintf(stderr,"Invalid keysym '%s' passed to SendRFBEvent\n",
		params[1]);
	return;
      }
      if (strcasecmp(params[0],"keydown") == 0) {
	SendKeyEvent(ks, 1);
      } else if (strcasecmp(params[0],"keyup") == 0) {
	SendKeyEvent(ks, 0);
      } else if (strcasecmp(params[0],"key") == 0) {
	SendKeyEvent(ks, 1);
	SendKeyEvent(ks, 0);
      } else {
	fprintf(stderr,"Invalid event '%s' passed to SendRFBEvent\n",
		params[0]);
	return;
      }
    } else if (strcasecmp(params[0],"fbupdate") == 0) {
      if (*num_params != 1) {
	fprintf(stderr, "Invalid params: SendRFBEvent(fbupdate)\n");
	return;
      }
      SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
				   si.framebufferHeight, False);
    } else if (strcasecmp(params[0],"ptr") == 0) {
      if (*num_params == 4) {
	x = atoi(params[1]);
	y = atoi(params[2]);
	buttonMask = atoi(params[3]);
	SendPointerEvent(x, y, buttonMask);
      } else if (*num_params == 2) {
	switch (ev->type) {
	case ButtonPress:
	case ButtonRelease:
	  x = ev->xbutton.x;
	  y = ev->xbutton.y;
	  break;
	case KeyPress:
	case KeyRelease:
	  x = ev->xkey.x;
	  y = ev->xkey.y;
	  break;
	default:
	  fprintf(stderr,
		  "Invalid event caused SendRFBEvent(ptr,<buttonMask>)\n");
	  return;
	}
	buttonMask = atoi(params[1]);
	SendPointerEvent(x, y, buttonMask);
      } else {
	fprintf(stderr,
		"Invalid params: SendRFBEvent(ptr,<x>,<y>,<buttonMask>)\n"
		"             or SendRFBEvent(ptr,<buttonMask>)\n");
	return;
      }

    } else {
      fprintf(stderr,"Invalid event '%s' passed to SendRFBEvent\n", params[0]);
    }
    return;
  }

  switch (ev->type) {

  case MotionNotify:
    while (XCheckTypedWindowEvent(dpy, desktopWin, MotionNotify, ev))
      ;	/* discard all queued motion notify events */

    SendPointerEvent(ev->xmotion.x, ev->xmotion.y,
		     (ev->xmotion.state & 0x1f00) >> 8);
    return;

  case ButtonPress:
    SendPointerEvent(ev->xbutton.x, ev->xbutton.y,
		     (((ev->xbutton.state & 0x1f00) >> 8) |
		      (1 << (ev->xbutton.button - 1))));
    return;

  case ButtonRelease:
    SendPointerEvent(ev->xbutton.x, ev->xbutton.y,
		     (((ev->xbutton.state & 0x1f00) >> 8) &
		      ~(1 << (ev->xbutton.button - 1))));
    return;

  case KeyPress:
  case KeyRelease:
    XLookupString(&ev->xkey, keyname, 256, &ks, NULL);

    if (IsModifierKey(ks)) {
      ks = XKeycodeToKeysym(dpy, ev->xkey.keycode, 0);
      modifierPressed[ev->xkey.keycode] = (ev->type == KeyPress);
    }

    SendKeyEvent(ks, (ev->type == KeyPress));
    return;

  default:
    fprintf(stderr,"Invalid event passed to SendRFBEvent\n");
  }
}


/*
 * RepaintScreen is an action which erases the local framebuffer copy and
 * repaints the whole screen from scratch with a full update request.
 */

void
RepaintScreen(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  SoftCursorLockArea(0, 0, si.framebufferWidth, si.framebufferHeight);

  memset(image->data, 0, image->bytes_per_line * image->height);

  STATS(vncStats.blitPixels += (double)si.framebufferWidth * si.framebufferHeight);

#ifdef MITSHM
  if (appData.useShm) {
    STATS(vncStats.shmPutImages++);
    XShmPutImage(dpy, desktopWin, gc, image, 0, 0, 0, 0,
		 si.framebufferWidth, si.framebufferHeight, False);
  } else
#endif
  {
    STATS(vncStats.putImages++);
    XPutImage(dpy, desktopWin, gc, image, 0, 0, 0, 0,
	      si.framebufferWidth, si.framebufferHeight);
  }

  SoftCursorUnlockScreen();

  SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
			       si.framebufferHeight, False);
}


/*
 * LocalCursorName returns the name of the current local cursor mode, for
 * the F8 menu to put in its label.
 */

const char *
LocalCursorName(void)
{
  return cursorModeNames[cursorMode];
}


/*
 * SetLocalCursorState puts the current cursor mode into the label of the
 * widget it is given.  The F8 menu draws its own label from
 * LocalCursorName(), so this is only useful if bound to a widget that has a
 * label resource.
 */

void
SetLocalCursorState(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  char label[32];

  if (!w || !XtIsWidget(w))
    return;

  sprintf(label, "Local cursor: %s", cursorModeNames[cursorMode]);
  XtVaSetValues(w, XtNlabel, label, NULL);
}


/*
 * CycleLocalCursor is an action which cycles the local cursor between the
 * dot, a real arrow, and no cursor at all (remote only).
 */

void
CycleLocalCursor(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  cursorMode = (cursorMode + 1) % 3;

  /* stop remote cursor shapes overriding the explicit choice */
  appData.useX11Cursor = False;

  XDefineCursor(dpy, desktopWin, CursorForMode());
  fprintf(stderr, "Local cursor: %s\n", cursorModeNames[cursorMode]);
}


/*
 * CreateDotCursor.
 */

static Cursor
CreateDotCursor()
{
  Cursor cursor;
  Pixmap src, msk;
  static char srcBits[] = { 0, 14,14,14, 0 };
  static char mskBits[] = { 14,31,31,31,14 };
  XColor fg, bg;

  src = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), srcBits, 5, 5);
  msk = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), mskBits, 5, 5);
  XAllocNamedColor(dpy, DefaultColormap(dpy,DefaultScreen(dpy)), "black",
		   &fg, &fg);
  XAllocNamedColor(dpy, DefaultColormap(dpy,DefaultScreen(dpy)), "white",
		   &bg, &bg);
  cursor = XCreatePixmapCursor(dpy, src, msk, &fg, &bg, 2, 2);
  XFreePixmap(dpy, src);
  XFreePixmap(dpy, msk);

  return cursor;
}


/*
 * CursorForMode returns (lazily creating) the X cursor for the current local
 * cursor mode.
 */

static Cursor
CursorForMode(void)
{
  static Cursor arrowCursor = None, blankCursor = None;
  static char noData[] = { 0 };
  Pixmap p;
  XColor c;

  switch (cursorMode) {

  case CURSOR_ARROW:
    if (arrowCursor == None)
      arrowCursor = XCreateFontCursor(dpy, XC_left_ptr);
    return arrowCursor;

  case CURSOR_NONE:
    if (blankCursor == None) {
      p = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), noData, 1, 1);
      memset(&c, 0, sizeof(c));
      blankCursor = XCreatePixmapCursor(dpy, p, p, &c, &c, 0, 0);
      XFreePixmap(dpy, p);
    }
    return blankCursor;

  default:
    if (dotCursor == None)
      dotCursor = CreateDotCursor();
    return dotCursor;
  }
}


/*
 * BlitToImage translates a rectangle of server format pixels into an XImage
 * in the local visual's format.  The framebuffer goes through
 * CopyDataToImage(), the cursor shape through CreateLocalImage().
 */

static void
BlitToImage(XImage *img, char *buf, int x, int y, int width, int height)
{
  if (!useColorMap) {
    int h;
    int widthInBytes = width * myFormat.bitsPerPixel / 8;
    int scrWidthInBytes = img->bytes_per_line;

    char *scr = (img->data + y * scrWidthInBytes
		 + x * myFormat.bitsPerPixel / 8);

    /* A full width rectangle is contiguous in the image, so it goes in one
       move instead of one per scan line. */
    if (widthInBytes == scrWidthInBytes) {
      memcpy(scr, buf, (size_t)widthInBytes * height);
    } else {
      for (h = 0; h < height; h++) {
	memcpy(scr, buf, widthInBytes);
	buf += widthInBytes;
	scr += scrWidthInBytes;
      }
    }
  } else {
    CopyBGR233ToImage(img, (CARD8 *)buf, x, y, width, height);
  }
}

/*
 * CopyDataToImage puts pixels into the local framebuffer image without
 * telling X about them, PutImageRect issues the X request.  Decoders that
 * produce a rectangle in pieces - ZRLE tile by tile, Tight band by band -
 * blit each piece and then send one request for the whole rectangle, rather
 * than one request per piece.
 */

void
CopyDataToImage(char *buf, int x, int y, int width, int height)
{
  if (appData.rawDelay != 0) {
    XFillRectangle(dpy, desktopWin, gc, x, y, width, height);

    XSync(dpy,False);

    Msleep(appData.rawDelay);
  }

  BlitToImage(image, buf, x, y, width, height);

  STATS(vncStats.blitPixels += (double)width * height);
}


/*
 * CreateLocalImage builds a standalone XImage in the local visual's format
 * out of a rectangle of server format pixels.  cursor.c fills the cursor
 * pixmap through it, so that drawing the cursor is a copy on the X server
 * rather than a conversion here.
 */

XImage *
CreateLocalImage(char *buf, int width, int height)
{
  XImage *img = XCreateImage(dpy, vis, visdepth, ZPixmap, 0, NULL,
			     width, height, BitmapPad(dpy), 0);

  if (!img)
    return NULL;

  img->data = malloc(img->bytes_per_line * img->height);
  if (!img->data) {
    XDestroyImage(img);
    return NULL;
  }

  BlitToImage(img, buf, 0, 0, width, height);
  return img;
}

void
PutImageRect(int x, int y, int width, int height)
{
  int vx, vy, vw, vh;

  if (image == NULL || width <= 0 || height <= 0)
    return;

  /* The desktop widget is larger than the viewport whenever the remote
     desktop does not fit, and the server throws away whatever falls outside.
     Clip to the visible area instead of shipping those pixels.  The image
     still holds the whole framebuffer, so ScrollRepaint() can put back
     whatever scrolls into view later. */

  ScrollGetPos(&vx, &vy);
  ScrollGetVisible(&vw, &vh);

  if (vw > 0 && vh > 0) {
    if (x < vx) { width -= vx - x; x = vx; }
    if (y < vy) { height -= vy - y; y = vy; }
    if (x + width > vx + vw) width = vx + vw - x;
    if (y + height > vy + vh) height = vy + vh - y;
    if (width <= 0 || height <= 0)
      return;
  }

#ifdef MITSHM
  if (appData.useShm) {
    STATS(vncStats.shmPutImages++);
    XShmPutImage(dpy, desktopWin, gc, image, x, y, x, y, width, height, False);
    return;
  }
#endif
  STATS(vncStats.putImages++);
  XPutImage(dpy, desktopWin, gc, image, x, y, x, y, width, height);
}

void
CopyDataToScreen(char *buf, int x, int y, int width, int height)
{
  CopyDataToImage(buf, x, y, width, height);
  PutImageRect(x, y, width, height);
}


/*
 * CopyBGR233ToScreen.
 *
 * This runs for every pixel of every rectangle whenever the server sends one
 * of the reduced 8bpp formats, which is the default, so the per pixel loop
 * overhead is worth removing.  Unrolling by eight is +36% on PA-RISC; going
 * further and composing colorToPixel[] with the Tight palette to drop the
 * second pass only bought another 5 points, which did not justify writing
 * the filters straight into the strided image.
 */

#define BGR233_ROWS(scr, stridePixels)					\
  for (q = 0; q < height; q++) {					\
    p = width;								\
    while (p >= 8) {							\
      (scr)[0] = colorToPixel[buf[0]];					\
      (scr)[1] = colorToPixel[buf[1]];					\
      (scr)[2] = colorToPixel[buf[2]];					\
      (scr)[3] = colorToPixel[buf[3]];					\
      (scr)[4] = colorToPixel[buf[4]];					\
      (scr)[5] = colorToPixel[buf[5]];					\
      (scr)[6] = colorToPixel[buf[6]];					\
      (scr)[7] = colorToPixel[buf[7]];					\
      (scr) += 8;							\
      buf += 8;								\
      p -= 8;								\
    }									\
    while (p-- > 0)							\
      *((scr)++) = colorToPixel[*(buf++)];				\
    (scr) += (stridePixels) - width;					\
  }

static void
CopyBGR233ToImage(XImage *img, CARD8 *buf, int x, int y, int width, int height)
{
  int p, q;
  int xoff = 7 - (x & 7);
  int xcur;
  /* row stride comes from the XImage - scanlines are padded, so it is not
     necessarily framebufferWidth * bpp / 8 */
  int fbwb = img->bytes_per_line;
  CARD8 *scr1 = ((CARD8 *)img->data) + y * fbwb + x / 8;
  CARD8 *scrt;
  CARD8 *scr8 = ((CARD8 *)img->data) + y * fbwb + x;
  CARD16 *scr16 = (CARD16 *)(img->data + y * fbwb) + x;
  CARD32 *scr32 = (CARD32 *)(img->data + y * fbwb) + x;

  switch (visbpp) {

    /* thanks to Chris Hooper for single bpp support */

  case 1:
    for (q = 0; q < height; q++) {
      xcur = xoff;
      scrt = scr1;
      for (p = 0; p < width; p++) {
	*scrt = ((*scrt & ~(1 << xcur))
		 | (colorToPixel[*(buf++)] << xcur));

	if (xcur-- == 0) {
	  xcur = 7;
	  scrt++;
	}
      }
      scr1 += fbwb;
    }
    break;

  case 8:
    BGR233_ROWS(scr8, fbwb);
    break;

  case 16:
    BGR233_ROWS(scr16, fbwb / 2);
    break;

  case 32:
    BGR233_ROWS(scr32, fbwb / 4);
    break;
  }
}
