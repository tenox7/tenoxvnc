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
 * dialogs.c - the connection dialog, drawn with xwidgets.
 *
 * Asks for the server and the password together, before connecting, along
 * with the options people usually want to change.  The password is held
 * until the server actually asks for it; a server that needs no
 * authentication simply never uses it.
 *
 * The same panel without the server and password fields is the settings
 * dialog the F8 menu opens, so that what can be set before connecting can be
 * changed afterwards as well.
 */

#include "vncviewer.h"
#include "xwidgets.h"

#define HOST_LEN	255
#define PASS_LEN	63

enum { RES_CONNECT = 1, RES_CANCEL = 2 };

/* What the panel is being used for.  CONNECT has every field, PASSWORD only
   the password, SETTINGS only the options. */
enum { DLG_CONNECT, DLG_PASSWORD, DLG_SETTINGS };

static XwPanel dlg;
static int passItem;
static Bool dlgInUse = False;

static char dlgHost[HOST_LEN + 1];
static char dlgPass[PASS_LEN + 1];
Bool connectDialogUsed = False;

/* The notches on the three sliders.  Depth has to be one the X server can
   really give us, so the list is the true color depths rather than a range.
   The first entry of the other two is the unset one: depth 0 means any depth
   will do and compression level -1 asks the server for nothing, leaving the
   viewer to pick.  JPEG quality has no unset - out of range means 5 - so it
   runs 0 to 9 and says so. */
static const int depthVals[] = { 0, 8, 15, 16, 24, 32 };
static const int qualityVals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const int levelVals[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

/* The two radio groups.  Auto leaves the encoding to the viewer, which is
   what the viewer has always done; the rest of the encodings the viewer can
   decode are still reachable with -encodings, they are just not worth a
   button here.  The color levels are all the server can be asked for. */
static const char *const encNames[] = {
  "Auto", "Tight", "ZRLE", "Hextile", "Raw"
};
static const int encVals[] = {
  -1, rfbEncodingTight, rfbEncodingZRLE, rfbEncodingHextile, rfbEncodingRaw
};

static const char *const colorNames[] = {
  "Full", "Medium", "Low", "Very low"
};
static const int colorVals[] = {
  COLOR_FULL, COLOR_MEDIUM, COLOR_LOW, COLOR_VERYLOW
};


/*
 * CancelDialog is what the window manager's close button does to the dialog:
 * the same as pressing Cancel.  The dialog handles the rest of its own keys
 * and buttons itself.
 */

void
CancelDialog(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  dlg.result = RES_CANCEL;
  dlg.done = True;
}


/*
 * A radio group is laid out as a grid rather than a column, which would make
 * the dialog far taller than it needs to be.  cellW is the column pitch: the
 * group's own, from DlgCellW, unless it sits above rows of checkboxes, in
 * which case it takes theirs so that the columns line up down the section.
 * DlgRadios returns the y just below the last row.
 */

static int
DlgCellW(const char *const *names, int n)
{
  int i, w = 0;

  for (i = 0; i < n; i++)
    if (XwCheckW(names[i]) > w)
      w = XwCheckW(names[i]);

  return w + xwCharW * 2;
}

static int
DlgRadios(const char *const *names, const int *vals, int n, int *value,
	  int ncols, int cellW, int x, int y, int rowH, Bool disabled)
{
  int i;

  for (i = 0; i < n; i++)
    XwAddRadio(&dlg, names[i], value, vals[i],
	       x + (i % ncols) * cellW, y + (i / ncols) * rowH)
      ->disabled = disabled;

  return y + ((n + ncols - 1) / ncols) * rowH;
}

static void
DlgBuild(int mode)
{
  int pad = xwCharW * 2;
  int col1 = pad + XwStrW("Password:") + xwCharW;
  int col2, y, rowH = xwLineH + 6;
  int fieldCols = 26;
  int slider[3], i;
  Bool withOptions = (mode != DLG_PASSWORD);
  const char *okLabel = (mode == DLG_SETTINGS) ? "Apply" : "Connect";

  /* Three things the settings panel cannot offer.  A visual is fixed for the
     life of a window, so the settings that pick one are connect time only.
     The color level is fixed too where a forced visual dictated the format,
     and both it and the shared flag need a reconnect, which a session we did
     not dial in the first place has no way to make. */
  Bool visualFixed = (mode == DLG_SETTINGS);
  Bool colorFixed = (mode == DLG_SETTINGS) &&
		    (listenSpecified || !ColorLevelSettable());
  Bool sessionFixed = (mode == DLG_SETTINGS) && listenSpecified;

  XwReset(&dlg);
  passItem = -1;

  y = pad;
  if (dlg.message)
    y += xwLineH;

  if (mode == DLG_CONNECT) {
    /* A server named on the command line never went through this field, so
       fill it in from the one we are talking to - a reconnect that has to
       ask for the password again should not ask for the host as well. */
    if (dlgHost[0] == '\0' && vncServerHost[0] != '\0') {
      if (vncServerPort >= SERVER_PORT_OFFSET &&
	  vncServerPort < SERVER_PORT_OFFSET + 100)
	sprintf(dlgHost, "%.240s:%d", vncServerHost,
		vncServerPort - SERVER_PORT_OFFSET);
      else
	sprintf(dlgHost, "%.240s::%d", vncServerHost, vncServerPort);
    }

    XwAddLabel(&dlg, "Server:", pad, y + 2);
    XwAddText(&dlg, dlgHost, HOST_LEN, col1, y, fieldCols, False, False);
    y += rowH;
  }

  if (mode != DLG_SETTINGS) {
    XwAddLabel(&dlg, "Password:", pad, y + 2);
    XwAddText(&dlg, dlgPass, PASS_LEN, col1, y, fieldCols, True, False);
    passItem = dlg.nItems - 1;
    y += rowH;
  }

  if (withOptions) {
    /* The second column has to clear the widest checkbox that shares a row
       with it, not the widest overall - the long ones sit alone. */
    col2 = col1 + xwCharW * 2;
    col2 += XwCheckW("Shared") > XwCheckW("True color")
	    ? XwCheckW("Shared") : XwCheckW("True color");

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Session:", pad, y);
    XwAddCheck(&dlg, "Shared", &appData.shareDesktop, 0, col1, y)
      ->disabled = sessionFixed;
    XwAddCheck(&dlg, "View only", &appData.viewOnly, 0, col2, y);
    y += rowH;
    XwAddCheck(&dlg, "Continuous updates", &appData.useContinuousUpdates,
	       0, col1, y);
    y += rowH;
    XwAddCheck(&dlg, "Resize remote desktop", &appData.useRemoteResize,
	       0, col1, y);
    y += rowH;

    /* Each slider sits under the section it belongs to, and the two that
       only mean something with their checkbox ticked say so by grey.  The
       sliders are stretched to the finished width further down, the same way
       the F8 menu handles its separators. */

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Encoding:", pad, y);
    y = DlgRadios(encNames, encVals, XtNumber(encNames),
		  &appData.preferredEncoding, 3,
		  DlgCellW(encNames, XtNumber(encNames)), col1, y, rowH,
		  False);
    XwAddCheck(&dlg, "JPEG", &appData.enableJPEG, 0, col1, y);
    y += rowH;
    XwAddLabel(&dlg, "Quality", pad, y + 1)->enableIf = &appData.enableJPEG;
    slider[0] = dlg.nItems;
    XwAddSlider(&dlg, &appData.qualityLevel, qualityVals,
		XtNumber(qualityVals), False, col1, y, 1);
    dlg.items[slider[0]].enableIf = &appData.enableJPEG;
    y += rowH + 3;
    XwAddLabel(&dlg, "Compress", pad, y + 1);
    slider[1] = dlg.nItems;
    XwAddSlider(&dlg, &appData.compressLevel, levelVals,
		XtNumber(levelVals), True, col1, y, 1);
    y += rowH;

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Color:", pad, y);
    y = DlgRadios(colorNames, colorVals, XtNumber(colorNames),
		  &appData.colorLevel, 2, col2 - col1, col1, y, rowH,
		  colorFixed);
    XwAddCheck(&dlg, "True color", &appData.forceTrueColor, 0, col1, y)
      ->disabled = visualFixed;
    XwAddCheck(&dlg, "Own colormap", &appData.forceOwnCmap, 0, col2, y)
      ->disabled = visualFixed;
    y += rowH;
    XwAddLabel(&dlg, "Depth", pad, y + 1)->enableIf = &appData.forceTrueColor;
    dlg.items[dlg.nItems - 1].disabled = visualFixed;
    slider[2] = dlg.nItems;
    XwAddSlider(&dlg, &appData.requestedDepth, depthVals,
		XtNumber(depthVals), True, col1, y, 1);
    dlg.items[slider[2]].enableIf = &appData.forceTrueColor;
    dlg.items[slider[2]].disabled = visualFixed;
    y += rowH;
  }

  /* Size from what was actually laid out, so nothing can be clipped by a
     width guessed in advance.  The message is drawn rather than laid out as
     an item, so it has to be measured here or a long one would be cut off. */
  dlg.w = XwContentWidth(&dlg) + pad;

  if (dlg.message && dlg.w < pad * 2 + XwStrW(dlg.message))
    dlg.w = pad * 2 + XwStrW(dlg.message);

  y += xwLineH / 2;
  {
    int bw = XwStrW(okLabel) + 4 * xwCharW;
    int cw = XwStrW("Cancel") + 4 * xwCharW;
    int gap = xwCharW * 2;

    if (dlg.w < pad * 2 + bw + cw + gap)
      dlg.w = pad * 2 + bw + cw + gap;

    XwAddButton(&dlg, okLabel, RES_CONNECT, dlg.w - pad - cw - gap - bw, y);
    XwAddButton(&dlg, "Cancel", RES_CANCEL, dlg.w - pad - cw, y);
    y += xwLineH + 6;
  }

  if (withOptions)
    for (i = 0; i < 3; i++)
      dlg.items[slider[i]].w = dlg.w - pad - col1;

  dlg.h = y + pad;

  /* A message means something went wrong with what was typed last time,
     which is nearly always the password, so start there. */
  if (dlg.message && passItem >= 0)
    dlg.focus = passItem;
}


static int
DlgRun(int mode, const char *title, const char *message)
{
  int res;

  /* There is one panel, and it is modal only in the sense that it runs its
     own event loop - it takes no X grab, so F8 still reaches the desktop
     while it is up and can ask for a second one.  Refuse that rather than
     build over the panel the outer loop is still running. */
  if (!XwInit() || dlgInUse)
    return RES_CANCEL;

  dlgInUse = True;
  dlg.message = message;
  DlgBuild(mode);

  XwBuildWindow(&dlg, "connectDialog", title, dlg.w, dlg.h, True);
  XwPlaceCentred(&dlg);
  XwPopup(&dlg);

  res = XwRunModal(&dlg);
  if (res != RES_CONNECT)
    res = RES_CANCEL;

  XwPopdown(&dlg);
  XwDestroy(&dlg);
  dlgInUse = False;

  return res;
}


/*
 * DoConnectDialog asks for the server, the password and the common options
 * in one box.  message is shown in red above the fields and is NULL on the
 * first call.  Cancelling quits.
 */

char *
DoConnectDialog(const char *message)
{
  for (;;) {
    if (DlgRun(DLG_CONNECT, "TenoxVNC " TENOXVNC_VERSION " - Connect", message)
	!= RES_CONNECT) {
      Cleanup();
      exit(1);
    }

    if (dlgHost[0] != '\0')
      break;

    message = "Enter the VNC server to connect to.";
  }

  connectDialogUsed = True;
  return XtNewString(dlgHost);
}


/*
 * AskForServer puts the connection dialog up with message above the fields
 * and keeps it up until what was typed parses as a server name.
 */

void
AskForServer(const char *message)
{
  for (;;) {
    char *name = DoConnectDialog(message);
    Bool ok = SetServerName(name);

    XtFree(name);
    if (ok)
      return;

    message = connErrorMsg;
  }
}


/*
 * DoPasswordDialog is called from the authentication path.  If the
 * connection dialog was used the password has already been typed, so hand
 * that over; otherwise put up a box asking only for the password.
 */

char *
DoPasswordDialog()
{
  if (connectDialogUsed)
    return dlgPass;

  if (DlgRun(DLG_PASSWORD, "TenoxVNC - Password", NULL) != RES_CONNECT) {
    Cleanup();
    exit(1);
  }

  return dlgPass;
}


/*
 * ForgetPassword clears the typed password after a failed authentication,
 * so the retry starts with an empty field.
 */

void
ForgetPassword(void)
{
  memset(dlgPass, 0, sizeof(dlgPass));
  ForgetSessionPassword();
}


/*
 * DoSettingsDialog is the F8 "Settings..." panel: the options half of the
 * connection dialog, with the server and password fields left off and the
 * settings that cannot change under a live window greyed out.  Returns True
 * if Apply was pressed, in which case appData holds the new values; the
 * caller keeps a copy of the old ones to put back on Cancel and to work out
 * what actually changed.
 */

Bool
DoSettingsDialog(void)
{
  return DlgRun(DLG_SETTINGS, "TenoxVNC - Settings", NULL) == RES_CONNECT;
}
