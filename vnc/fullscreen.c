/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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
 * fullscreen.c - full-screen mode.
 *
 * The toplevel is made override-redirect so the window manager leaves it
 * alone, and sized to the whole display.  scroll.c then gives the entire
 * area to the desktop with no scrollbars, and bump scrolling moves it when
 * the desktop is bigger than the screen.
 *
 * The old version of this file was written against the Athena Viewport: it
 * drove the widget's forceBars resource, reached into its private "clip"
 * child to measure the scrollbars, and re-targeted Form constraints to move
 * the viewport about.  Owning the scrolling outright makes all of that go
 * away - full screen is now just a resize plus ScrollTo().
 */

#include <vncviewer.h>

static void BumpScrollTimerCallback(XtPointer clientData, XtIntervalId *id);
static Bool DoBumpScroll(void);

static XtIntervalId timer;
static Bool timerSet = False;
static Bool scrollLeft, scrollRight, scrollUp, scrollDown;

/* geometry to put back when leaving full-screen mode */
static Dimension savedWidth, savedHeight;


/*
 * FullScreenOn goes into full-screen mode.
 */

void
FullScreenOn()
{
  XtVaGetValues(toplevel, XtNwidth, &savedWidth, XtNheight, &savedHeight,
		NULL);

  appData.fullScreen = True;
  ScrollAllowBars(False);

  /* Unmap first: a window manager that has already reparented us will not
     notice overrideRedirect changing underneath it. */
  XtUnmapWidget(toplevel);
  XtVaSetValues(toplevel, XtNoverrideRedirect, True, NULL);
  XtVaSetValues(toplevel, XtNmaxWidth, 32767, XtNmaxHeight, 32767, NULL);
  XtResizeWidget(toplevel, dpyWidth, dpyHeight, 0);
  XtMoveWidget(toplevel, 0, 0);
  XtMapWidget(toplevel);
  XRaiseWindow(dpy, XtWindow(toplevel));
  XSync(dpy, False);
}


/*
 * FullScreenOff returns to a normal window.
 */

void
FullScreenOff()
{
  Dimension w = savedWidth, h = savedHeight;

  appData.fullScreen = False;

  if (timerSet) {
    XtRemoveTimeOut(timer);
    timerSet = False;
  }

  ScrollAllowBars(True);
  ScrollTo(0, 0);

  if (w == 0 || w > dpyWidth)
    w = si.framebufferWidth;
  if (h == 0 || h > dpyHeight)
    h = si.framebufferHeight;

  if (w + appData.wmDecorationWidth >= dpyWidth)
    w = dpyWidth - appData.wmDecorationWidth;
  if (h + appData.wmDecorationHeight >= dpyHeight)
    h = dpyHeight - appData.wmDecorationHeight;

  XtUnmapWidget(toplevel);
  XtVaSetValues(toplevel, XtNoverrideRedirect, False, NULL);
  XtResizeWidget(toplevel, w, h, 0);
  XtMapWidget(toplevel);
  XSync(dpy, False);
}


/*
 * ToggleFullScreen is an action which toggles in and out of full-screen
 * mode.
 */

void
ToggleFullScreen(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  if (appData.fullScreen)
    FullScreenOff();
  else
    FullScreenOn();
}


/*
 * SetFullScreenState was an action which set the "state" resource of an
 * Athena toggle to match.  The F8 menu draws its own checkbox from
 * appData.fullScreen now, so this only does anything if someone has bound it
 * to a widget that has a state resource.
 */

void
SetFullScreenState(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  if (!w || !XtIsWidget(w))
    return;

  XtVaSetValues(w, "state", appData.fullScreen, NULL);
}


/*
 * BumpScroll is called when in full-screen mode and the mouse is against one
 * of the edges of the screen.  It returns true if any scrolling was done.
 */

Bool
BumpScroll(XEvent *ev)
{
  scrollLeft = scrollRight = scrollUp = scrollDown = False;

  if (ev->xmotion.x_root >= dpyWidth - 3)
    scrollRight = True;
  else if (ev->xmotion.x_root <= 2)
    scrollLeft = True;

  if (ev->xmotion.y_root >= dpyHeight - 3)
    scrollDown = True;
  else if (ev->xmotion.y_root <= 2)
    scrollUp = True;

  if (scrollLeft || scrollRight || scrollUp || scrollDown) {
    if (timerSet)
      return True;
    return DoBumpScroll();
  }

  if (timerSet) {
    XtRemoveTimeOut(timer);
    timerSet = False;
  }

  return False;
}

static Bool
DoBumpScroll(void)
{
  int x, y, oldx, oldy, visW, visH;

  ScrollGetPos(&x, &y);
  ScrollGetVisible(&visW, &visH);
  oldx = x;
  oldy = y;

  if (scrollRight)
    x += appData.bumpScrollPixels;
  else if (scrollLeft)
    x -= appData.bumpScrollPixels;

  if (scrollDown)
    y += appData.bumpScrollPixels;
  else if (scrollUp)
    y -= appData.bumpScrollPixels;

  if (x > si.framebufferWidth - visW)
    x = si.framebufferWidth - visW;
  if (y > si.framebufferHeight - visH)
    y = si.framebufferHeight - visH;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  if (x == oldx && y == oldy) {
    timerSet = False;
    return False;
  }

  ScrollTo(x, y);
  timer = XtAppAddTimeOut(appContext, appData.bumpScrollTime,
			  BumpScrollTimerCallback, NULL);
  timerSet = True;
  return True;
}

static void
BumpScrollTimerCallback(XtPointer clientData, XtIntervalId *id)
{
  timerSet = False;
  DoBumpScroll();
}
