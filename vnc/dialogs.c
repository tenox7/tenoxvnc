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
 * dialogs.c - the connection dialog.
 *
 * Drawn with plain Xlib on a bare Xt core widget, the same way stats.c draws
 * the diagnostics window.  It used to be built from Athena widgets, which
 * cost the whole Xaw Text/AsciiSrc/AsciiSink machinery for two text fields
 * and meant no dialogs at all on systems without Athena - DECwindows on VMS
 * has Xlib, Xt, Xmu and Xext but no Xaw whatsoever.
 *
 * The dialog asks for the server and the password together, before
 * connecting, along with the options people usually want to change.  The
 * password is held until the server actually asks for it; a server that
 * needs no authentication simply never uses it.
 *
 * Everything here runs before SetVisualAndCmap(), so the default visual and
 * colormap are still current and the colours mean what they say.
 */

#include "vncviewer.h"
#include <X11/Xutil.h>
#include <ctype.h>

#define DLG_MAXITEMS	48
#define HOST_LEN	255
#define PASS_LEN	63
#define NUM_LEN		3

enum {				/* item kinds */
  IT_LABEL,
  IT_TEXT,
  IT_CHECK,
  IT_BUTTON
};

enum {				/* button ids, also the dialog result */
  RES_NONE = 0,
  RES_CONNECT,
  RES_CANCEL
};

typedef struct {
  int kind;
  const char *label;
  int x, y, w, h;

  char *buf;			/* IT_TEXT */
  int maxlen;
  int caret;
  Bool secret;
  Bool numeric;

  Bool *flag;			/* IT_CHECK */

  int id;			/* IT_BUTTON */
} Item;

static Item items[DLG_MAXITEMS];
static int nItems;
static int focusItem = -1;
static int passItem = -1;
static int result;
static Bool dlgDone;

static Widget dlgShell, dlgCanvas;
static Window dlgWin;
static Pixmap dlgBuf;
static GC dgc;
static XFontStruct *dlgFont;
static int lineH, charW, dlgW, dlgH;
static const char *dlgMessage;

static unsigned long cBg, cFg, cField, cDark, cLight, cWarn;

/* Filled in by the dialog, read back by the rest of the viewer. */
static char dlgHost[HOST_LEN + 1];
static char dlgPass[PASS_LEN + 1];
Bool connectDialogUsed = False;

static char numDepth[NUM_LEN + 1], numQuality[NUM_LEN + 1];
static char numCompress[NUM_LEN + 1];

static void DlgEvent(Widget w, XtPointer p, XEvent *ev, Boolean *cont);


/*
 * The two actions below are still named in the table in argsresources.c.
 * The Athena dialogs used them to notice Return being pressed in a text
 * field; this dialog handles its own keys, so they do nothing.
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


/*
 * Small helpers.  A font that is certain to exist everywhere and a colour
 * that degrades to black or white rather than failing.
 */

static XFontStruct *
DlgLoadFont(void)
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
DlgColour(const char *spec, unsigned long fallback)
{
  XColor screen, exact;
  int scr = DefaultScreen(dpy);

  if (XAllocNamedColor(dpy, DefaultColormap(dpy, scr), spec, &screen, &exact))
    return screen.pixel;

  return fallback;
}


/*
 * Item construction.  Coordinates are worked out from the font metrics as
 * the items are added, so the dialog fits whatever font was available.
 */

static Item *
DlgAdd(int kind, const char *label, int x, int y, int w, int h)
{
  Item *it;

  if (nItems >= DLG_MAXITEMS)
    return &items[DLG_MAXITEMS - 1];	/* should not happen */

  it = &items[nItems++];
  memset(it, 0, sizeof(*it));
  it->kind = kind;
  it->label = label;
  it->x = x;
  it->y = y;
  it->w = w;
  it->h = h;
  return it;
}

static void
DlgLabel(const char *text, int x, int y)
{
  DlgAdd(IT_LABEL, text, x, y, XTextWidth(dlgFont, text, strlen(text)), lineH);
}

static void
DlgText(char *buf, int maxlen, int x, int y, int cols, Bool secret,
	Bool numeric)
{
  Item *it = DlgAdd(IT_TEXT, NULL, x, y, cols * charW + 6, lineH + 4);

  it->buf = buf;
  it->maxlen = maxlen;
  it->caret = strlen(buf);
  it->secret = secret;
  it->numeric = numeric;

  if (focusItem < 0)
    focusItem = nItems - 1;
}

static void
DlgCheck(const char *label, Bool *flag, int x, int y)
{
  Item *it = DlgAdd(IT_CHECK, label, x, y,
		    lineH + 4 + XTextWidth(dlgFont, label, strlen(label)),
		    lineH);

  it->flag = flag;
}

static void
DlgButton(const char *label, int id, int x, int y)
{
  Item *it = DlgAdd(IT_BUTTON, label, x, y,
		    XTextWidth(dlgFont, label, strlen(label)) + 4 * charW,
		    lineH + 6);

  it->id = id;
}


/*
 * Drawing.  Everything goes to an offscreen pixmap and is copied over in one
 * go, so the dialog never flickers while a field is being typed into.
 */

static void
DlgFrame(int x, int y, int w, int h, Bool sunken)
{
  XSetForeground(dpy, dgc, sunken ? cDark : cLight);
  XDrawLine(dpy, dlgBuf, dgc, x, y, x + w - 1, y);
  XDrawLine(dpy, dlgBuf, dgc, x, y, x, y + h - 1);
  XSetForeground(dpy, dgc, sunken ? cLight : cDark);
  XDrawLine(dpy, dlgBuf, dgc, x, y + h - 1, x + w - 1, y + h - 1);
  XDrawLine(dpy, dlgBuf, dgc, x + w - 1, y, x + w - 1, y + h - 1);
}

static void
DlgDrawItem(int i)
{
  Item *it = &items[i];
  int tx, ty, n, boxY;
  const char *s;
  char stars[PASS_LEN + 1];

  ty = it->y + dlgFont->ascent;

  switch (it->kind) {

  case IT_LABEL:
    XSetForeground(dpy, dgc, cFg);
    XDrawString(dpy, dlgBuf, dgc, it->x, ty, it->label, strlen(it->label));
    break;

  case IT_TEXT:
    XSetForeground(dpy, dgc, cField);
    XFillRectangle(dpy, dlgBuf, dgc, it->x, it->y, it->w, it->h);
    DlgFrame(it->x, it->y, it->w, it->h, True);

    s = it->buf;
    n = strlen(it->buf);
    if (it->secret) {
      for (n = 0; it->buf[n] && n < PASS_LEN; n++)
	stars[n] = '*';
      stars[n] = '\0';
      s = stars;
    }

    XSetForeground(dpy, dgc, cFg);
    XDrawString(dpy, dlgBuf, dgc, it->x + 3, it->y + 2 + dlgFont->ascent,
		s, n);

    if (i == focusItem) {
      tx = it->x + 3 + XTextWidth(dlgFont, s, it->caret);
      XDrawLine(dpy, dlgBuf, dgc, tx, it->y + 2,
		tx, it->y + it->h - 3);
      /* a focused field gets a heavier border so the caret is not the only
	 clue, which matters on monochrome and on very small fonts */
      XDrawRectangle(dpy, dlgBuf, dgc, it->x - 1, it->y - 1,
		     it->w + 1, it->h + 1);
    }
    break;

  case IT_CHECK:
    boxY = it->y + (lineH - (lineH - 4)) / 2;
    n = lineH - 4;
    XSetForeground(dpy, dgc, cField);
    XFillRectangle(dpy, dlgBuf, dgc, it->x, boxY, n, n);
    DlgFrame(it->x, boxY, n, n, True);

    if (*it->flag) {
      XSetForeground(dpy, dgc, cFg);
      XDrawLine(dpy, dlgBuf, dgc, it->x + 3, boxY + n / 2,
		it->x + n / 2 - 1, boxY + n - 4);
      XDrawLine(dpy, dlgBuf, dgc, it->x + 3, boxY + n / 2 - 1,
		it->x + n / 2 - 1, boxY + n - 5);
      XDrawLine(dpy, dlgBuf, dgc, it->x + n / 2 - 1, boxY + n - 4,
		it->x + n - 3, boxY + 3);
      XDrawLine(dpy, dlgBuf, dgc, it->x + n / 2 - 1, boxY + n - 5,
		it->x + n - 3, boxY + 2);
    }

    XSetForeground(dpy, dgc, cFg);
    XDrawString(dpy, dlgBuf, dgc, it->x + n + 6, ty,
		it->label, strlen(it->label));

    if (i == focusItem)
      XDrawRectangle(dpy, dlgBuf, dgc, it->x - 2, it->y - 1,
		     it->w + 3, it->h + 1);
    break;

  case IT_BUTTON:
    XSetForeground(dpy, dgc, cBg);
    XFillRectangle(dpy, dlgBuf, dgc, it->x, it->y, it->w, it->h);
    DlgFrame(it->x, it->y, it->w, it->h, False);
    XSetForeground(dpy, dgc, cFg);
    tx = it->x + (it->w - XTextWidth(dlgFont, it->label,
				     strlen(it->label))) / 2;
    XDrawString(dpy, dlgBuf, dgc, tx, it->y + 3 + dlgFont->ascent,
		it->label, strlen(it->label));
    if (i == focusItem)
      XDrawRectangle(dpy, dlgBuf, dgc, it->x + 2, it->y + 2,
		     it->w - 5, it->h - 5);
    break;
  }
}

static void
DlgRedraw(void)
{
  int i;

  if (!dlgBuf)
    return;

  XSetForeground(dpy, dgc, cBg);
  XFillRectangle(dpy, dlgBuf, dgc, 0, 0, dlgW, dlgH);

  if (dlgMessage) {
    XSetForeground(dpy, dgc, cWarn);
    XDrawString(dpy, dlgBuf, dgc, charW, lineH + dlgFont->ascent - 2,
		dlgMessage, strlen(dlgMessage));
  }

  for (i = 0; i < nItems; i++)
    DlgDrawItem(i);

  XCopyArea(dpy, dlgBuf, dlgWin, dgc, 0, 0, dlgW, dlgH, 0, 0);
}


/*
 * Focus and hit testing.
 */

static Bool
DlgFocusable(int i)
{
  return items[i].kind == IT_TEXT || items[i].kind == IT_CHECK ||
	 items[i].kind == IT_BUTTON;
}

static void
DlgMoveFocus(int dir)
{
  int i, n = nItems;

  if (n == 0)
    return;

  for (i = 0; i < n; i++) {
    focusItem += dir;
    if (focusItem >= n)
      focusItem = 0;
    if (focusItem < 0)
      focusItem = n - 1;
    if (DlgFocusable(focusItem))
      return;
  }
}

static int
DlgHit(int x, int y)
{
  int i;

  for (i = 0; i < nItems; i++) {
    if (!DlgFocusable(i))
      continue;
    if (x >= items[i].x - 2 && x < items[i].x + items[i].w + 2 &&
	y >= items[i].y - 2 && y < items[i].y + items[i].h + 2)
      return i;
  }
  return -1;
}


/*
 * Text field editing.
 */

static void
DlgInsert(Item *it, char c)
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
DlgBackspace(Item *it)
{
  int n = strlen(it->buf);

  if (it->caret == 0)
    return;
  memmove(it->buf + it->caret - 1, it->buf + it->caret, n - it->caret + 1);
  it->caret--;
}

static void
DlgDelete(Item *it)
{
  int n = strlen(it->buf);

  if (it->caret >= n)
    return;
  memmove(it->buf + it->caret, it->buf + it->caret + 1, n - it->caret);
}

static void
DlgKey(XKeyEvent *ev)
{
  char text[32];
  KeySym ks;
  int n, i;
  Item *it = focusItem >= 0 ? &items[focusItem] : NULL;

  n = XLookupString(ev, text, sizeof(text) - 1, &ks, NULL);

  switch (ks) {

  case XK_Tab:
    DlgMoveFocus((ev->state & ShiftMask) ? -1 : 1);
    return;

  /* ISO_Left_Tab arrived in X11R6; DECwindows and other R5 servers do not
     have it, so shift-tab there comes through as XK_Tab plus ShiftMask. */
#ifdef XK_ISO_Left_Tab
  case XK_ISO_Left_Tab:
#endif
  case XK_Up:
    DlgMoveFocus(-1);
    return;

  case XK_Down:
    DlgMoveFocus(1);
    return;

  case XK_Return:
  case XK_KP_Enter:
    if (it && it->kind == IT_BUTTON)
      result = it->id;
    else if (it && it->kind == IT_CHECK)
      *it->flag = !*it->flag;
    else
      result = RES_CONNECT;
    if (result != RES_NONE)
      dlgDone = True;
    return;

  case XK_Escape:
    result = RES_CANCEL;
    dlgDone = True;
    return;

  case XK_space:
    if (it && it->kind == IT_CHECK) {
      *it->flag = !*it->flag;
      return;
    }
    if (it && it->kind == IT_BUTTON) {
      result = it->id;
      dlgDone = True;
      return;
    }
    break;			/* a space in a text field is just a space */
  }

  if (!it || it->kind != IT_TEXT)
    return;

  switch (ks) {
  case XK_BackSpace:
    DlgBackspace(it);
    return;
  case XK_Delete:
  case XK_KP_Delete:
    DlgDelete(it);
    return;
  case XK_Left:
    if (it->caret > 0)
      it->caret--;
    return;
  case XK_Right:
    if (it->caret < (int)strlen(it->buf))
      it->caret++;
    return;
  case XK_Home:
  case XK_Begin:
    it->caret = 0;
    return;
  case XK_End:
    it->caret = strlen(it->buf);
    return;
  }

  if (ev->state & ControlMask) {
    if (ks == XK_u || ks == XK_U) {		/* clear the field */
      it->buf[0] = '\0';
      it->caret = 0;
    }
    return;
  }

  for (i = 0; i < n; i++)
    if (isprint((unsigned char)text[i]))
      DlgInsert(it, text[i]);
}


static void
DlgEvent(Widget w, XtPointer p, XEvent *ev, Boolean *cont)
{
  int hit;

  switch (ev->type) {

  case Expose:
    if (ev->xexpose.count == 0)
      DlgRedraw();
    return;

  case ButtonPress:
    hit = DlgHit(ev->xbutton.x, ev->xbutton.y);
    if (hit < 0)
      return;
    focusItem = hit;

    if (items[hit].kind == IT_CHECK) {
      *items[hit].flag = !*items[hit].flag;
    } else if (items[hit].kind == IT_BUTTON) {
      result = items[hit].id;
      dlgDone = True;
    } else if (items[hit].kind == IT_TEXT) {
      Item *it = &items[hit];
      int cx = ev->xbutton.x - it->x - 3;
      int len = strlen(it->buf);
      int k;

      /* put the caret at the character nearest the click */
      it->caret = len;
      for (k = 0; k <= len; k++) {
	int px = it->secret ? k * XTextWidth(dlgFont, "*", 1)
			    : XTextWidth(dlgFont, it->buf, k);
	if (px >= cx) {
	  it->caret = k;
	  break;
	}
      }
    }
    DlgRedraw();
    return;

  case KeyPress:
    DlgKey(&ev->xkey);
    if (!dlgDone)
      DlgRedraw();
    return;
  }
}


/*
 * Build the dialog.  withOptions is false for the password-only case, where
 * the server was given on the command line and only the password is wanted.
 */

static int
DlgStrW(const char *s)
{
  return XTextWidth(dlgFont, s, strlen(s));
}

/* width a checkbox needs: the box, a gap, and the label */
static int
DlgCheckW(const char *s)
{
  return lineH - 4 + 6 + DlgStrW(s);
}

static int
DlgMax4(int a, int b, int c, int d)
{
  if (b > a) a = b;
  if (c > a) a = c;
  if (d > a) a = d;
  return a;
}

static void
DlgBuild(Bool withOptions)
{
  int pad = charW * 2;
  int col1 = pad + DlgStrW("Password:") + charW;
  int col2, numX, y, rowH = lineH + 6;
  int fieldCols = 26;
  int i, contentW;

  nItems = 0;
  focusItem = -1;
  passItem = -1;

  y = pad;
  if (dlgMessage)
    y += lineH;

  if (withOptions) {
    DlgLabel("Server:", pad, y + 2);
    DlgText(dlgHost, HOST_LEN, col1, y, fieldCols, False, False);
    y += rowH;
  }

  DlgLabel("Password:", pad, y + 2);
  DlgText(dlgPass, PASS_LEN, col1, y, fieldCols, True, False);
  passItem = nItems - 1;
  y += rowH;

  if (withOptions) {
    /* The second column has to clear the widest checkbox that shares a row
       with it - not the widest one overall, since the long ones sit alone. */
    col2 = col1 + DlgMax4(DlgCheckW("Shared"), DlgCheckW("BGR233"),
			  DlgCheckW("Own colormap"), DlgCheckW("JPEG"))
	   + charW * 2;
    numX = col2 + DlgMax4(DlgStrW("Depth:"), DlgStrW("Quality:"),
			  DlgStrW("Compress:"), 0) + charW;

    y += lineH / 2;
    DlgLabel("Session:", pad, y);
    DlgCheck("Shared", &appData.shareDesktop, col1, y);
    DlgCheck("View only", &appData.viewOnly, col2, y);
    y += rowH;
    DlgCheck("Continuous updates", &appData.useContinuousUpdates, col1, y);
    y += rowH;
    DlgCheck("Resize remote desktop", &appData.useRemoteResize, col1, y);
    y += rowH;

    y += lineH / 2;
    DlgLabel("Colour:", pad, y);
    DlgCheck("BGR233", &appData.useBGR233, col1, y);
    DlgCheck("True colour", &appData.forceTrueColour, col2, y);
    y += rowH;
    DlgCheck("Own colormap", &appData.forceOwnCmap, col1, y);
    DlgLabel("Depth:", col2, y);
    DlgText(numDepth, NUM_LEN, numX, y - 2, 3, False, True);
    y += rowH;

    y += lineH / 2;
    DlgLabel("Encoding:", pad, y);
    DlgCheck("JPEG", &appData.enableJPEG, col1, y);
    DlgLabel("Quality:", col2, y);
    DlgText(numQuality, NUM_LEN, numX, y - 2, 3, False, True);
    y += rowH;
    DlgLabel("Compress:", col2, y);
    DlgText(numCompress, NUM_LEN, numX, y - 2, 3, False, True);
    y += rowH;
  }

  /* Size the dialog from what was actually laid out, so nothing can be
     clipped by a width guessed in advance. */
  contentW = 0;
  for (i = 0; i < nItems; i++)
    if (items[i].x + items[i].w > contentW)
      contentW = items[i].x + items[i].w;

  dlgW = contentW + pad;

  y += lineH / 2;
  {
    int bw = DlgStrW("Connect") + 4 * charW;
    int cw = DlgStrW("Cancel") + 4 * charW;
    int gap = charW * 2;

    if (dlgW < pad * 2 + bw + cw + gap)
      dlgW = pad * 2 + bw + cw + gap;

    DlgButton("Connect", RES_CONNECT, dlgW - pad - cw - gap - bw, y);
    DlgButton("Cancel", RES_CANCEL, dlgW - pad - cw, y);
    y += lineH + 6;
  }

  dlgH = y + pad;

  /* A message means something went wrong with what was typed last time -
     which is nearly always the password, so start there. */
  if (dlgMessage && passItem >= 0)
    focusItem = passItem;
}


/*
 * Numeric option fields are text, so that "unset" can be shown as an empty
 * box: depth 0 and compression level -1 both mean "let the viewer decide".
 */

static void
DlgNumToText(char *buf, int value, int unset)
{
  if (value == unset)
    buf[0] = '\0';
  else
    sprintf(buf, "%d", value);
}

static int
DlgTextToNum(const char *buf, int unset, int lo, int hi)
{
  int v;

  if (buf[0] == '\0')
    return unset;
  v = atoi(buf);
  if (v < lo || v > hi)
    return unset;
  return v;
}


/*
 * Run the dialog.  Returns RES_CONNECT or RES_CANCEL.
 */

static int
DlgRun(Bool withOptions, const char *title, const char *message)
{
  int scr = DefaultScreen(dpy);

  dlgMessage = message;

  if (!dlgFont) {
    dlgFont = DlgLoadFont();
    if (!dlgFont) {
      fprintf(stderr, "%s: no usable font for the connection dialog\n",
	      programName);
      return RES_CANCEL;
    }
    lineH = dlgFont->ascent + dlgFont->descent + 2;
    charW = XTextWidth(dlgFont, "n", 1);
    if (charW < 1)
      charW = 6;

    cBg = DlgColour("gray85", WhitePixel(dpy, scr));
    cFg = DlgColour("black", BlackPixel(dpy, scr));
    cField = DlgColour("white", WhitePixel(dpy, scr));
    cDark = DlgColour("gray45", BlackPixel(dpy, scr));
    cLight = DlgColour("gray100", WhitePixel(dpy, scr));
    cWarn = DlgColour("red3", BlackPixel(dpy, scr));
  }

  DlgNumToText(numDepth, appData.requestedDepth, 0);
  DlgNumToText(numQuality, appData.qualityLevel, -1);
  DlgNumToText(numCompress, appData.compressLevel, -1);

  DlgBuild(withOptions);

  /* Default visual and colormap on purpose - see the note at the top. */
  dlgShell = XtVaCreatePopupShell("connectDialog", topLevelShellWidgetClass,
				  toplevel,
				  XtNvisual, DefaultVisual(dpy, scr),
				  XtNdepth, DefaultDepth(dpy, scr),
				  XtNcolormap, DefaultColormap(dpy, scr),
				  XtNtitle, title,
				  XtNiconName, title,
				  XtNwidth, dlgW,
				  XtNheight, dlgH,
				  XtNminWidth, dlgW,
				  XtNminHeight, dlgH,
				  XtNmaxWidth, dlgW,
				  XtNmaxHeight, dlgH,
				  XtNinput, True,
				  NULL);

  dlgCanvas = XtVaCreateManagedWidget("connectCanvas", coreWidgetClass,
				      dlgShell,
				      XtNwidth, dlgW,
				      XtNheight, dlgH,
				      XtNbackground, cBg,
				      XtNborderWidth, 0,
				      NULL);

  XtAddEventHandler(dlgCanvas,
		    ExposureMask | ButtonPressMask | KeyPressMask,
		    False, DlgEvent, NULL);

  XtRealizeWidget(dlgShell);
  dlgWin = XtWindow(dlgCanvas);
  dgc = XCreateGC(dpy, dlgWin, 0, NULL);
  XSetFont(dpy, dgc, dlgFont->fid);
  dlgBuf = XCreatePixmap(dpy, dlgWin, dlgW, dlgH, DefaultDepth(dpy, scr));

  XtVaSetValues(dlgShell,
		XtNx, (WidthOfScreen(XtScreen(dlgShell)) - dlgW) / 2,
		XtNy, (HeightOfScreen(XtScreen(dlgShell)) - dlgH) / 3, NULL);

  XtPopup(dlgShell, XtGrabNone);
  XSetWMProtocols(dpy, XtWindow(dlgShell), &wmDeleteWindow, 1);
  DlgRedraw();

  result = RES_NONE;
  dlgDone = False;
  while (!dlgDone)
    XtAppProcessEvent(appContext, XtIMAll);

  if (result == RES_CONNECT && withOptions) {
    appData.requestedDepth = DlgTextToNum(numDepth, 0, 1, 32);
    appData.qualityLevel = DlgTextToNum(numQuality, -1, 0, 9);
    appData.compressLevel = DlgTextToNum(numCompress, -1, 0, 9);
  }

  XtPopdown(dlgShell);
  XFreePixmap(dpy, dlgBuf);
  dlgBuf = None;
  XFreeGC(dpy, dgc);
  XtDestroyWidget(dlgShell);
  dlgShell = NULL;
  XSync(dpy, False);

  return result;
}


/*
 * DoConnectDialog asks for the server, the password and the common options
 * in one box.  Returns the server name, or does not return at all if the
 * dialog is cancelled.  message is shown in red above the fields and is
 * NULL on the first call.
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
