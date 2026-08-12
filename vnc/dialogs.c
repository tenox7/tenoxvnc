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
#define NUM_LEN		3

enum { RES_CONNECT = 1, RES_CANCEL = 2 };

static XwPanel dlg;
static int passItem;

static char dlgHost[HOST_LEN + 1];
static char dlgPass[PASS_LEN + 1];
Bool connectDialogUsed = False;

static char numDepth[NUM_LEN + 1], numQuality[NUM_LEN + 1];
static char numCompress[NUM_LEN + 1];


/*
 * These two are still named in the actions table in argsresources.c.  The
 * dialog handles its own keys, so they do nothing.
 */

void
ServerDialogDone(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
}

void
PasswordDialogDone(Widget w, XEvent *event, String *params,
		   Cardinal *num_params)
{
}


static int
Max4(int a, int b, int c, int d)
{
  if (b > a) a = b;
  if (c > a) a = c;
  if (d > a) a = d;
  return a;
}

/*
 * Numeric option fields are text so that "unset" can be an empty box: depth
 * 0 and compression level -1 both mean "let the viewer decide".
 */

static void
NumToText(char *buf, int value, int unset)
{
  if (value == unset)
    buf[0] = '\0';
  else
    sprintf(buf, "%d", value);
}

static int
TextToNum(const char *buf, int unset, int lo, int hi)
{
  int v;

  if (buf[0] == '\0')
    return unset;
  v = atoi(buf);
  if (v < lo || v > hi)
    return unset;
  return v;
}


static void
DlgBuild(Bool withOptions)
{
  int pad = xwCharW * 2;
  int col1 = pad + XwStrW("Password:") + xwCharW;
  int col2, numX, y, rowH = xwLineH + 6;
  int fieldCols = 26;

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
    numX = col2 + Max4(XwStrW("Depth:"), XwStrW("Quality:"),
		       XwStrW("Compress:"), 0) + xwCharW;

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

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Colour:", pad, y);
    XwAddCheck(&dlg, "BGR233", &appData.useBGR233, 0, col1, y);
    XwAddCheck(&dlg, "True colour", &appData.forceTrueColour, 0, col2, y);
    y += rowH;
    XwAddCheck(&dlg, "Own colormap", &appData.forceOwnCmap, 0, col1, y);
    XwAddLabel(&dlg, "Depth:", col2, y);
    XwAddText(&dlg, numDepth, NUM_LEN, numX, y - 2, 3, False, True);
    y += rowH;

    y += xwLineH / 2;
    XwAddLabel(&dlg, "Encoding:", pad, y);
    XwAddCheck(&dlg, "JPEG", &appData.enableJPEG, 0, col1, y);
    XwAddLabel(&dlg, "Quality:", col2, y);
    XwAddText(&dlg, numQuality, NUM_LEN, numX, y - 2, 3, False, True);
    y += rowH;
    XwAddLabel(&dlg, "Compress:", col2, y);
    XwAddText(&dlg, numCompress, NUM_LEN, numX, y - 2, 3, False, True);
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

  NumToText(numDepth, appData.requestedDepth, 0);
  NumToText(numQuality, appData.qualityLevel, -1);
  NumToText(numCompress, appData.compressLevel, -1);

  dlg.message = message;
  DlgBuild(withOptions);

  XwBuildWindow(&dlg, "connectDialog", title, dlg.w, dlg.h, True);
  XwPlaceCentred(&dlg);
  XwPopup(&dlg);

  res = XwRunModal(&dlg);
  if (res != RES_CONNECT)
    res = RES_CANCEL;

  if (res == RES_CONNECT && withOptions) {
    appData.requestedDepth = TextToNum(numDepth, 0, 1, 32);
    appData.qualityLevel = TextToNum(numQuality, -1, 0, 9);
    appData.compressLevel = TextToNum(numCompress, -1, 0, 9);
  }

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
