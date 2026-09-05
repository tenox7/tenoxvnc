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
 * A panel the viewer draws itself: a framed section for each thing the menu
 * deals with, the settings that can be on or off as checkboxes showing the
 * real state, everything else as buttons, and the two that finish with the
 * menu in a row along the bottom.
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
  P_HWCURSOR,
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
static int cursorItem = -1, cuItem = -1;
static char cursorLabel[40];

static void MenuActivate(int id);


/*
 * Run one of the viewer's actions by name.  The action procedures take a
 * widget, which the ones reached from here ignore; they are given the
 * desktop widget so anything looking at XtDisplay/XtScreen still works.
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


/*
 * Layout.  Inside a section everything sits in one of two columns of equal
 * width, so the controls line up down the whole menu however wide the font
 * makes them.  None of those widths are known while the items are being
 * added, so each one is marked with what it wants instead and the lot is
 * stretched to fit at the end - the way the connection dialog settles the
 * width of its sliders.
 */

#define S_FRAME	1		/* a section frame: the full panel width */
#define S_COL	2		/* one column, and has a say in how wide it is */
#define S_SPAN	4		/* both columns */
#define S_RIGHT	8		/* belongs in the second column */

static int stretch[XW_MAXITEMS];
static int section;		/* frame of the section being filled in */

static void
Mark(int how)
{
  stretch[menu.nItems - 1] = how;
}

static int
SectionStart(const char *title, int y)
{
  XwAddGroup(&menu, title, 0, y, 1, 1);
  section = menu.nItems - 1;
  Mark(S_FRAME);

  return y + xwLineH + 2;	/* clear of the title let into the top edge */
}

static int
SectionEnd(int y)
{
  y += xwLineH / 3;
  menu.items[section].h = y - menu.items[section].y;

  return y + xwLineH / 2;
}


static void
MenuBuild(void)
{
  int pad = xwCharW * 2;	/* panel edge to a section frame */
  int gin = xwCharW + xwCharW / 2;	/* frame to the controls inside it */
  int gap = xwCharW;		/* between the two columns */
  int rowH = xwLineH + 6;
  int col1 = pad + gin;
  int y = pad;
  int bw, inner, i;
  int diagW = XwStrW("Diagnostics...") + 4 * xwCharW;
  int dismissW = XwStrW("Dismiss") + 4 * xwCharW;
  int quitW = XwStrW("Quit viewer") + 4 * xwCharW;

  XwReset(&menu);
  memset(stretch, 0, sizeof(stretch));

  /* The checkboxes point straight at the viewer's own state rather than at
     copies, so what they show cannot drift from what is really set. */

  y = SectionStart("Screen", y);
  XwAddCheck(&menu, "Full screen", &appData.fullScreen, P_FULLSCREEN, col1, y);
  Mark(S_COL);
  menu.focus = menu.nItems - 1;
  XwAddCheck(&menu, "Continuous updates", &cuActive, P_CONTINUOUS, col1, y);
  Mark(S_COL | S_RIGHT);
  cuItem = menu.nItems - 1;
  menu.items[cuItem].disabled = !supportsCU;
  y += rowH;
  XwAddButton(&menu, "Repaint screen", P_REPAINT, col1, y);
  Mark(S_COL);
  XwAddButton(&menu, "Request refresh", P_REFRESH, col1, y);
  Mark(S_COL | S_RIGHT);
  y = SectionEnd(y + rowH);

  y = SectionStart("Cursor", y);
  sprintf(cursorLabel, "Local cursor: %s", LocalCursorName());
  XwAddButton(&menu, cursorLabel, P_CURSOR, col1, y);
  Mark(S_SPAN);
  cursorItem = menu.nItems - 1;
  y += rowH;
  XwAddCheck(&menu, "X server draws remote cursor", &appData.useHwCursor,
	     P_HWCURSOR, col1, y);
  Mark(S_SPAN);
  y = SectionEnd(y + xwLineH);

  y = SectionStart("Clipboard", y);
  XwAddButton(&menu, "Local -> remote", P_CLIP_OUT, col1, y);
  Mark(S_COL);
  XwAddButton(&menu, "Local <- remote", P_CLIP_IN, col1, y);
  Mark(S_COL | S_RIGHT);
  y = SectionEnd(y + rowH);

  y = SectionStart("Send keys", y);
  XwAddButton(&menu, "Ctrl-Alt-Del", P_CTRLALTDEL, col1, y);
  Mark(S_COL);
  XwAddButton(&menu, "F8", P_SENDF8, col1, y);
  Mark(S_COL | S_RIGHT);
  y = SectionEnd(y + rowH);

  /* One width for every button in a column, wide enough for the widest of
     them and for the checkboxes sharing the columns with them. */
  bw = 0;
  for (i = 0; i < menu.nItems; i++)
    if ((stretch[i] & S_COL) && menu.items[i].w > bw)
      bw = menu.items[i].w;

  inner = bw * 2 + gap;

  for (i = 0; i < menu.nItems; i++)
    if ((stretch[i] & S_SPAN) && menu.items[i].w > inner)
      inner = menu.items[i].w;

  /* The bottom row runs the full width of the panel, frames and all, so it
     needs less of the inside of a section than its own width. */
  if (inner < diagW + dismissW + quitW + gap * 2 - gin * 2)
    inner = diagW + dismissW + quitW + gap * 2 - gin * 2;

  bw = (inner - gap) / 2;
  menu.w = col1 + inner + gin + pad;

  for (i = 0; i < menu.nItems; i++) {
    if (stretch[i] & S_FRAME) {
      menu.items[i].x = pad;
      menu.items[i].w = menu.w - pad * 2;
    }
    if (menu.items[i].kind == XW_BUTTON) {
      if (stretch[i] & S_COL)
	menu.items[i].w = bw;
      if (stretch[i] & S_SPAN)
	menu.items[i].w = inner;
    }
    if (stretch[i] & S_RIGHT)
      menu.items[i].x = col1 + bw + gap;
  }

  /* Diagnostics opens a window of its own, so it keeps the left of the
     bottom row and the two that finish with the menu sit at the right. */
  XwAddButton(&menu, "Diagnostics...", P_STATS, pad, y);
#ifndef VNCSTATS
  menu.items[menu.nItems - 1].disabled = True;	/* not compiled in */
#endif
  XwAddButton(&menu, "Dismiss", P_DISMISS,
	      menu.w - pad - quitW - gap - dismissW, y);
  XwAddButton(&menu, "Quit viewer", P_QUIT, menu.w - pad - quitW, y);

  menu.h = y + rowH + pad;
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
  XwBuildWindow(&menu, "popup", "TenoxVNC " TENOXVNC_VERSION,
		menu.w, menu.h, False);
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
 * RefreshPopup re-reads what the menu shows and repaints it.  Some of that
 * changes behind the menu's back: switching continuous updates off only
 * takes effect when the server answers with EndOfContinuousUpdates, and the
 * same message is what announces support for them in the first place.
 */

void
RefreshPopup(void)
{
  if (!menuUp)
    return;

  if (cuItem >= 0)
    menu.items[cuItem].disabled = !supportsCU;

  if (cursorItem >= 0) {
    sprintf(cursorLabel, "Local cursor: %s", LocalCursorName());
    menu.items[cursorItem].label = cursorLabel;
  }

  XwRedraw(&menu);
}


/*
 * MenuActivate is called by xwidgets the moment something is clicked.  It
 * cannot rebuild the panel, since it runs inside that panel's own event
 * handler, so entries that change update in place through RefreshPopup().
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
    RefreshPopup();
    return;

  case P_CURSOR:
    RunAction("CycleLocalCursor", NULL, 0);
    RefreshPopup();
    return;

  case P_HWCURSOR:
    RunAction("ToggleHardwareCursor", NULL, 0);
    RefreshPopup();
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
