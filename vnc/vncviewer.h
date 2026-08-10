/*
 *  Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
 *  Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
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
 * vncviewer.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef _AIX
#include <sys/select.h>	/* AIX keeps fd_set here, not in types.h/time.h */
#include <strings.h>	/* FD_ZERO expands to bzero() */
#endif
#include <unistd.h>
#include <pwd.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xmd.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xmu/StdSel.h>
#include "rfbproto.h"
#include "caps.h"

extern int endianTest;

#define Swap16IfLE(s) \
    (*(char *)&endianTest ? ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)) : (s))

#define Swap32IfLE(l) \
    (*(char *)&endianTest ? ((((l) & 0xff000000) >> 24) | \
			     (((l) & 0x00ff0000) >> 8)  | \
			     (((l) & 0x0000ff00) << 8)  | \
			     (((l) & 0x000000ff) << 24))  : (l))

#define MAX_ENCODINGS 32

/* Limits on server-controlled lengths and dimensions (malicious-server hardening). */
#define RFB_BUFFER_SIZE (640*480)
#define RFB_TIGHT_MAX_WIDTH 2048
#define RFB_MAX_STRING_LENGTH 65536
#define RFB_MAX_CURSOR_DIMENSION 4096
#define RFB_MAX_RRE_SUBRECTS 65535
#define RFB_MAX_ALLOC_SIZE ((size_t)0x7fffffff)

#define LISTEN_PORT_OFFSET 5500
#define TUNNEL_PORT_OFFSET 5500
#define SERVER_PORT_OFFSET 5900

#define DEFAULT_SSH_CMD "/usr/bin/ssh"
#define DEFAULT_TUNNEL_CMD  \
  (DEFAULT_SSH_CMD " -f -L %L:localhost:%R %H sleep 20")
#define DEFAULT_VIA_CMD     \
  (DEFAULT_SSH_CMD " -f -L %L:%H:%R %G sleep 20")


/* argsresources.c */

typedef struct {
  Bool shareDesktop;
  Bool viewOnly;
  Bool fullScreen;
  Bool grabKeyboard;
  Bool raiseOnBeep;

  String encodingsString;

  Bool useBGR233;
  int nColours;
  Bool useSharedColours;
  Bool forceOwnCmap;
  Bool forceTrueColour;
  int requestedDepth;

  Bool useShm;

  int wmDecorationWidth;
  int wmDecorationHeight;

  char *userLogin;

  char *passwordFile;
  Bool passwordDialog;

  int rawDelay;
  int copyRectDelay;

  Bool debug;

  int popupButtonCount;

  int bumpScrollTime;
  int bumpScrollPixels;

  int compressLevel;
  int qualityLevel;
  Bool enableJPEG;
  Bool useRemoteCursor;
  Bool useX11Cursor;
  String localCursor;
  Bool autoPass;

  Bool useRemoteResize;
  Bool useContinuousUpdates;

} AppData;

extern AppData appData;

extern char *fallback_resources[];
extern char vncServerHost[];
extern int vncServerPort;
extern Bool listenSpecified;
extern int listenPort;

extern XrmOptionDescRec cmdLineOptions[];
extern int numCmdLineOptions;

extern void removeArgs(int *argc, char** argv, int idx, int nargs);
extern void usage(void);
extern void GetArgsAndResources(int argc, char **argv);

/* colour.c */

extern unsigned long BGR233ToPixel[];

extern Colormap cmap;
extern Visual *vis;
extern unsigned int visdepth, visbpp;

extern void SetVisualAndCmap();

/* cursor.c */

extern Bool HandleCursorShape(int xhot, int yhot, int width, int height,
                              CARD32 enc);
extern Bool HandleCursorPos(int x, int y);
extern void SoftCursorLockArea(int x, int y, int w, int h);
extern void SoftCursorUnlockScreen(void);
extern void SoftCursorMove(int x, int y);

/* desktop.c */

extern Atom wmDeleteWindow;
extern Widget form, viewport, desktop;
extern Window desktopWin;
extern Cursor dotCursor;
extern GC gc;
extern GC srcGC, dstGC;
extern Dimension dpyWidth, dpyHeight;

extern void DesktopInitBeforeRealization();
extern void DesktopInitAfterRealization();
extern void SendRFBEvent(Widget w, XEvent *event, String *params,
			 Cardinal *num_params);
extern void CopyDataToScreen(char *buf, int x, int y, int width, int height);
extern void RepaintScreen(Widget w, XEvent *event, String *params,
			  Cardinal *num_params);
extern void CycleLocalCursor(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern void SetLocalCursorState(Widget w, XEvent *event, String *params,
				Cardinal *num_params);
extern void SynchroniseScreen();
extern void ResizeDesktopFramebuffer(int width, int height);
extern void TrackDesktopResizes(void);
extern void DesktopSizeSupportLearned(void);

/* dialogs.c */

extern void ServerDialogDone(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern char *DoServerDialog();
extern void PasswordDialogDone(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern char *DoPasswordDialog();

/* fullscreen.c */

extern void ToggleFullScreen(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern void SetFullScreenState(Widget w, XEvent *event, String *params,
			       Cardinal *num_params);
extern Bool BumpScroll(XEvent *ev);
extern void FullScreenOn();
extern void FullScreenOff();

/* listen.c */

extern void listenForIncomingConnections(int *argc, char **argv,
					 int listenArgIndex);

/* misc.c */

extern void ToplevelInitBeforeRealization();
extern void ToplevelInitAfterRealization();
extern Time TimeFromEvent(XEvent *ev);
extern void UpdateWindowTitle(void);
extern void Msleep(int msec);
extern void Pause(Widget w, XEvent *event, String *params,
		  Cardinal *num_params);
extern void RunCommand(Widget w, XEvent *event, String *params,
		       Cardinal *num_params);
extern void Quit(Widget w, XEvent *event, String *params,
		 Cardinal *num_params);
extern void Cleanup();

extern Bool RfbMulSize(size_t a, size_t b, size_t c, size_t *result);
extern Bool RfbCheckAddSize(size_t base, size_t extra, size_t *result);
extern Bool RfbValidServerStringLength(CARD32 len, size_t extra);

/* popup.c */

extern Widget popup;
extern void ShowPopup(Widget w, XEvent *event, String *params,
		      Cardinal *num_params);
extern void HidePopup(Widget w, XEvent *event, String *params,
		      Cardinal *num_params);
extern void CreatePopup();

/* rfbproto.c */

extern int rfbsock;
extern Bool canUseCoRRE;
extern Bool canUseHextile;
extern char *desktopName;
extern rfbPixelFormat myFormat;
extern rfbServerInitMsg si;
extern char *serverCutText;
extern Bool newServerCutText;

extern int protocolMinorVersion;
extern Bool tightVncProtocol;
extern char titleEncName[];

extern Bool supportsSetDesktopSize;
extern Bool pendingDesktopResize;

extern Bool ConnectToRFBServer(const char *hostname, int port);
extern Bool InitialiseRFBConnection();
extern Bool SetFormatAndEncodings();
extern Bool SendIncrementalFramebufferUpdateRequest();
extern Bool SendFramebufferUpdateRequest(int x, int y, int w, int h,
					 Bool incremental);
extern Bool SendPointerEvent(int x, int y, int buttonMask);
extern Bool SendKeyEvent(CARD32 key, Bool down);
extern Bool SendClientCutText(char *str, int len);
extern Bool SendSetDesktopSize(int width, int height);
extern Bool SendEnableContinuousUpdates(Bool enable, int x, int y,
					int w, int h);
extern void ToggleContinuousUpdates(Widget w, XEvent *ev, String *params,
				    Cardinal *num_params);
extern void SetContinuousUpdatesState(Widget w, XEvent *ev, String *params,
				      Cardinal *num_params);
extern Bool SendFence(CARD32 flags, int len, char *data);
extern Bool HandleRFBServerMessage();

extern void PrintPixelFormat(rfbPixelFormat *format);

/* selection.c */

extern void InitialiseSelection();
extern void SelectionToVNC(Widget w, XEvent *event, String *params,
			   Cardinal *num_params);
extern void SelectionFromVNC(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);

/* shm.c */

extern XImage *CreateShmImage();
extern void ShmDetachImage(XImage *img);
extern void ShmCleanup();

/* sockets.c */

extern Bool errorMessageOnReadFailure;

extern Bool ReadFromRFBServer(char *out, unsigned int n);
extern Bool WriteExact(int sock, char *buf, int n);
extern int FindFreeTcpPort(void);
extern int ListenAtTcpPort(int port);
extern int ConnectToTcpAddr(unsigned int host, int port);
extern int AcceptTcpConnection(int listenSock);
extern Bool SetNonBlocking(int sock);

extern int StringToIPAddr(const char *str, unsigned int *addr);
extern Bool SameMachine(int sock);

/* tunnel.c */

extern Bool tunnelSpecified;

extern Bool createTunnel(int *argc, char **argv, int tunnelArgIndex);

/* vncviewer.c */

extern char *programName;
extern XtAppContext appContext;
extern Display* dpy;
extern Widget toplevel;

extern void ProcessPendingXEvents(void);
