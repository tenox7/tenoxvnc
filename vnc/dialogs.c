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
 */

#include "vncviewer.h"
#include "xwidgets.h"

#define HOST_LEN	255
#define PASS_LEN	63

enum { RES_CONNECT = 1, RES_CANCEL = 2 };

static XwPanel dlg;
static int passItem;

static char dlgHost[HOST_LEN + 1];
static char dlgPass[PASS_LEN + 1];
Bool connectDialogUsed = False;

/* The notches on the three sliders.  Depth has to be one the X server can
   really give us, so the list is the true colour depths rather than a range.
   The first entry of the other two is the unset one: depth 0 means any depth
   will do and compression level -1 asks the server for nothing, leaving the
   viewer to pick.  JPEG quality has no unset - out of range means 5 - so it
   runs 0 to 9 and says so. */
static const int depthVals[] = { 0, 8, 15, 16, 24, 32 };
static const int qualityVals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const int levelVals[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };


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


static int
Max4(int a, int b, int c, int d)
{
  if (b > a) a = b;
  if (c > a) a = c;
  if (d > a) a = d;
  return a;
}

static void
DlgBuild(Bool withOptions)
{
  int pad = xwCharW * 2;
  int col1 = pad + XwStrW("Password:") + xwCharW;
  int col2, y, rowH = xwLineH + 6;
  int fieldCols = 26;
  int slider[3], i;

  XwReset(&dlg);
  passItem = -1;

  y = pad;
  if (dlg.message)
    y += xwLineH;

  if (withOptions) {
    XwAddLabel(&dlg, "Server:", pad, y + 2);
    XwAddText(&dlg, dlgHost, HOST_LEN, col1, y, fieldCols, False, False);
    y += rowH;
  }

  XwAddLabel(&dlg, "Password:", pad, y + 2);
  XwAddText(&dlg, dlgPass, PASS_LEN, col1, y, fieldCols, True, False);
  passItem = dlg.nItems - 1;
  y += rowH;

  if (withOptions) {
    /* The second column has to clear the widest checkbox that shares a row
       with it, not the widest overall - the long ones sit alone. */
    col2 = col1 + Max4(XwCheckW("Shared"), XwCheckW("BGR233"),
		       XwCheckW("Own colormap"), XwCheckW("JPEG"))
	   + xwCharW * 2;

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Session:", pad, y);
    XwAddCheck(&dlg, "Shared", &appData.shareDesktop, 0, col1, y);
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
    XwAddLabel(&dlg, "Colour:", pad, y);
    XwAddCheck(&dlg, "BGR233", &appData.useBGR233, 0, col1, y);
    XwAddCheck(&dlg, "True colour", &appData.forceTrueColour, 0, col2, y);
    y += rowH;
    XwAddCheck(&dlg, "Own colormap", &appData.forceOwnCmap, 0, col1, y);
    y += rowH;
    XwAddLabel(&dlg, "Depth", pad, y + 1)->enableIf = &appData.forceTrueColour;
    slider[2] = dlg.nItems;
    XwAddSlider(&dlg, &appData.requestedDepth, depthVals,
		XtNumber(depthVals), True, col1, y, 1);
    dlg.items[slider[2]].enableIf = &appData.forceTrueColour;
    y += rowH;
  }

  /* Size from what was actually laid out, so nothing can be clipped by a
     width guessed in advance. */
  dlg.w = XwContentWidth(&dlg) + pad;

  y += xwLineH / 2;
  {
    int bw = XwStrW("Connect") + 4 * xwCharW;
    int cw = XwStrW("Cancel") + 4 * xwCharW;
    int gap = xwCharW * 2;

    if (dlg.w < pad * 2 + bw + cw + gap)
      dlg.w = pad * 2 + bw + cw + gap;

    XwAddButton(&dlg, "Connect", RES_CONNECT, dlg.w - pad - cw - gap - bw, y);
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
DlgRun(Bool withOptions, const char *title, const char *message)
{
  int res;

  if (!XwInit())
    return RES_CANCEL;

  dlg.message = message;
  DlgBuild(withOptions);

  XwBuildWindow(&dlg, "connectDialog", title, dlg.w, dlg.h, True);
  XwPlaceCentred(&dlg);
  XwPopup(&dlg);

  res = XwRunModal(&dlg);
  if (res != RES_CONNECT)
    res = RES_CANCEL;

  XwPopdown(&dlg);
  XwDestroy(&dlg);

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
    if (DlgRun(True, "TenoxVNC - Connect", message) != RES_CONNECT) {
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
 * DoPasswordDialog is called from the authentication path.  If the
 * connection dialog was used the password has already been typed, so hand
 * that over; otherwise put up a box asking only for the password.
 */

char *
DoPasswordDialog()
{
  if (connectDialogUsed)
    return dlgPass;

  if (DlgRun(False, "TenoxVNC - Password", NULL) != RES_CONNECT) {
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
}
