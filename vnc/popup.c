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
 * popup.c - the F8 menu, drawn with xwidgets.
 *
 * This used to be a column of Athena Command and Toggle widgets built from
 * resources, one per entry.  It is now a panel the viewer draws itself, with
 * a fixed set of entries: the two things that can be on or off are
 * checkboxes showing the real state, the rest are buttons.
 *
 * Everything here still goes through the Xt actions in the table in
 * argsresources.c, so all of it remains available to bind to a key with
 * -xrm, for example:
 *
 *   -xrm '*desktop.baseTranslations: <Key>F5: RepaintScreen()'
 */

#include "vncviewer.h"
#include "xwidgets.h"

Widget popup, fullScreenToggle;

enum {
  P_NONE = 0,
  P_FULLSCREEN,
  P_CONTINUOUS,
  P_CURSOR,
  P_CLIP_OUT,
  P_CLIP_IN,
  P_REFRESH,
  P_REPAINT,
  P_CTRLALTDEL,
  P_SENDF8,
  P_STATS,
  P_DISMISS,
  P_QUIT
};

static XwPanel menu;
static Bool menuBuilt = False;
static Bool menuUp = False;
static int cursorItem = -1;
static char cursorLabel[40];

static void MenuActivate(int id);

/* State the checkboxes show.  Read from the viewer each time the menu is
   opened rather than tracked here, so it cannot drift. */
static Bool fsState, cuState;


/*
 * Run one of the viewer's actions by name.  The action procedures take a
 * widget, which only the old Athena buttons ever used (to set their own
 * label or state); the ones reached from here ignore it, and are given the
 * desktop widget so that anything looking at XtDisplay/XtScreen still works.
 */

static void
RunAction(const char *name, String *params, Cardinal nparams)
{
  XEvent ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = ButtonPress;
  ev.xbutton.display = dpy;
  ev.xbutton.window = desktopWin;
  ev.xbutton.time = CurrentTime;

  XtCallActionProc(desktop, name, &ev, params, nparams);
}

static void
SendKeyCombo(const char *k1, const char *k2, const char *k3)
{
  String p[2];

  if (k1) {
    p[0] = (String)"keydown"; p[1] = (String)k1; RunAction("SendRFBEvent", p, 2);
  }
  if (k2) {
    p[0] = (String)"keydown"; p[1] = (String)k2; RunAction("SendRFBEvent", p, 2);
  }
  if (k3) {
    p[0] = (String)"key"; p[1] = (String)k3; RunAction("SendRFBEvent", p, 2);
  }
  if (k2) {
    p[0] = (String)"keyup"; p[1] = (String)k2; RunAction("SendRFBEvent", p, 2);
  }
  if (k1) {
    p[0] = (String)"keyup"; p[1] = (String)k1; RunAction("SendRFBEvent", p, 2);
  }
}


static void
MenuBuild(void)
{
  int pad = xwCharW * 2;
  int y = pad, rowH = xwLineH + 6;
  int w, sep1, sep2;

  fsState = appData.fullScreen;
  cuState = cuActive;

  XwReset(&menu);

  XwAddCheck(&menu, "Full screen", &fsState, P_FULLSCREEN, pad, y);
  y += rowH;
  XwAddCheck(&menu, "Continuous updates", &cuState, P_CONTINUOUS, pad, y);
  if (!supportsCU)
    menu.items[menu.nItems - 1].disabled = True;
  y += rowH;

  sprintf(cursorLabel, "Local cursor: %s", LocalCursorName());
  cursorItem = menu.nItems;
  XwAddButton(&menu, cursorLabel, P_CURSOR, pad, y);
  y += rowH + 2;

  /* separators are stretched to the finished width further down */
  sep1 = menu.nItems;
  XwAddSep(&menu, pad, y, 1);
  y += 6;

  XwAddButton(&menu, "Clipboard: local -> remote", P_CLIP_OUT, pad, y);
  y += rowH;
  XwAddButton(&menu, "Clipboard: local <- remote", P_CLIP_IN, pad, y);
  y += rowH;
  XwAddButton(&menu, "Request refresh", P_REFRESH, pad, y);
  y += rowH;
  XwAddButton(&menu, "Repaint screen", P_REPAINT, pad, y);
  y += rowH;
  XwAddButton(&menu, "Send ctrl-alt-del", P_CTRLALTDEL, pad, y);
  y += rowH;
  XwAddButton(&menu, "Send F8", P_SENDF8, pad, y);
  y += rowH;
#ifdef VNCSTATS
  XwAddButton(&menu, "Diagnostics...", P_STATS, pad, y);
  y += rowH;
#endif

  y += 2;
  sep2 = menu.nItems;
  XwAddSep(&menu, pad, y, 1);
  y += 8;

  w = XwContentWidth(&menu);

  {
    int dw = XwStrW("Dismiss") + 4 * xwCharW;
    int qw = XwStrW("Quit viewer") + 4 * xwCharW;

    menu.w = w + pad;
    if (menu.w < pad * 2 + dw + qw + xwCharW * 2)
      menu.w = pad * 2 + dw + qw + xwCharW * 2;

    XwAddButton(&menu, "Dismiss", P_DISMISS, pad, y);
    XwAddButton(&menu, "Quit viewer", P_QUIT, menu.w - pad - qw, y);
    y += xwLineH + 6;
  }

  /* now that the width is settled, run the separators the full way across */
  menu.items[sep1].w = menu.w - pad * 2;
  menu.items[sep2].w = menu.w - pad * 2;

  menu.h = y + pad;
  menu.focus = 0;
}


/*
 * CreatePopup is called at start-up.  The panel itself is not built until
 * the menu is first opened, because the font and the desktop widget have to
 * exist first.
 */

void
CreatePopup()
{
  popup = NULL;
}


void
ShowPopup(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  int x = 0, y = 0;

  if (!XwInit())
    return;

  if (menuUp) {
    HidePopup(w, event, params, num_params);
    return;
  }

  if (menuBuilt) {
    XwDestroy(&menu);
    menuBuilt = False;
  }

  MenuBuild();
  XwBuildWindow(&menu, "popup", "TenoxVNC", menu.w, menu.h, False);
  menu.activate = MenuActivate;
  menuBuilt = True;

  if (event && (event->type == ButtonPress || event->type == KeyPress)) {
    x = event->xbutton.x_root;
    y = event->xbutton.y_root;
  }
  XwPlaceAt(&menu, x, y);

  XwPopup(&menu);
  menuUp = True;
  popup = menu.shell;
}


void
HidePopup(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  if (!menuUp)
    return;

  XwPopdown(&menu);
  menuUp = False;
}


/*
 * MenuActivate is called by xwidgets the moment something is clicked.  It
 * cannot rebuild the panel, since it runs inside that panel's own event
 * handler, so the one entry with a changing label updates it in place.
 */

static void
MenuActivate(int id)
{
  switch (id) {

  case P_FULLSCREEN:
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("ToggleFullScreen", NULL, 0);
    return;

  case P_CONTINUOUS:
    RunAction("ToggleContinuousUpdates", NULL, 0);
    cuState = cuActive;
    XwRedraw(&menu);
    return;

  case P_CURSOR:
    RunAction("CycleLocalCursor", NULL, 0);
    sprintf(cursorLabel, "Local cursor: %s", LocalCursorName());
    if (cursorItem >= 0)
      menu.items[cursorItem].label = cursorLabel;
    XwRedraw(&menu);
    return;

  case P_CLIP_OUT: {
    String p[1];
    p[0] = (String)"always";
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("SelectionToVNC", p, 1);
    return;
  }

  case P_CLIP_IN: {
    String p[1];
    p[0] = (String)"always";
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("SelectionFromVNC", p, 1);
    return;
  }

  case P_REFRESH: {
    String p[1];
    p[0] = (String)"fbupdate";
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("SendRFBEvent", p, 1);
    return;
  }

  case P_REPAINT:
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("RepaintScreen", NULL, 0);
    return;

  case P_CTRLALTDEL:
    HidePopup(NULL, NULL, NULL, NULL);
    SendKeyCombo("Control_L", "Alt_L", "Delete");
    return;

  case P_SENDF8:
    HidePopup(NULL, NULL, NULL, NULL);
    SendKeyCombo(NULL, NULL, "F8");
    return;

#ifdef VNCSTATS
  case P_STATS:
    HidePopup(NULL, NULL, NULL, NULL);
    RunAction("ShowStats", NULL, 0);
    return;
#endif

  case P_QUIT:
    RunAction("Quit", NULL, 0);
    return;

  case P_DISMISS:
  default:			/* including -1 from Escape */
    HidePopup(NULL, NULL, NULL, NULL);
    return;
  }
}
