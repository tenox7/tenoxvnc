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
 * vncviewer.c - the Xt-based VNC viewer.
 */

#include "vncviewer.h"

char *programName;
XtAppContext appContext;
Display* dpy;

Widget toplevel;

/*
 * ProcessPendingXEvents - handle queued X events, due timers etc. without
 * blocking.  The classic viewer only processes X events when the socket
 * read would block; with TigerVNC continuous updates the socket may never
 * run dry on busy screens, so the message loop must yield to X explicitly
 * or user input starves.
 */

void
ProcessPendingXEvents(void)
{
  while (XtAppPending(appContext))
    XtAppProcessEvent(appContext, XtIMAll);
}

int
main(int argc, char **argv)
{
  int i;
  programName = argv[0];

  /* Handle -h/-help/--help before any Xt initialisation so that printing
     usage does not require an X display. */

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 ||
	strcmp(argv[i], "--help") == 0)
      usage();
  }

  /* The -listen option is used to make us a daemon process which listens for
     incoming connections from servers, rather than actively connecting to a
     given server. The -tunnel and -via options are useful to create
     connections tunneled via SSH port forwarding. We must test for the
     -listen option before invoking any Xt functions - this is because we use
     forking, and Xt doesn't seem to cope with forking very well. For -listen
     option, when a successful incoming connection has been accepted,
     listenForIncomingConnections() returns, setting the listenSpecified
     flag. */

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-listen") == 0) {
      listenForIncomingConnections(&argc, argv, i);
      break;
    }
    if (strcmp(argv[i], "-tunnel") == 0 || strcmp(argv[i], "-via") == 0) {
      if (!createTunnel(&argc, argv, i))
	exit(1);
      break;
    }
  }

  /* Call the main Xt initialisation function.  It parses command-line options,
     generating appropriate resource specs, and makes a connection to the X
     display. */

  toplevel = XtVaAppInitialize(&appContext, "Vncviewer",
			       cmdLineOptions, numCmdLineOptions,
			       &argc, argv, fallback_resources,
			       XtNborderWidth, 0, NULL);

  dpy = XtDisplay(toplevel);

  /* The connection dialog below puts up a window of its own long before the
     toplevel is realized, and it needs this to tell the window manager what
     its close button means. */

  wmDeleteWindow = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

  /* Interpret resource specs and process any remaining command-line arguments
     (i.e. the VNC server name).  If the server name isn't specified on the
     command line, getArgsAndResources() will pop up a dialog box and wait
     for one to be entered. */

  GetArgsAndResources(argc, argv);

  /* Unless we accepted an incoming connection, make a TCP connection to the
     given VNC server, and initialise the VNC connection, which includes
     reading the password.

     A rejected password is worth a second chance when the connection dialog
     is in play: the server drops the connection at that point, so the retry
     has to redial from scratch. */

  if (listenSpecified) {
    if (!InitialiseRFBConnection()) exit(1);
  } else {
    while (1) {
      if (!ConnectToRFBServer(vncServerHost, vncServerPort)) exit(1);

      authFailed = False;
      if (InitialiseRFBConnection())
	break;

      if (!authFailed || !connectDialogUsed)
	exit(1);

      close(rfbsock);
      ForgetPassword();
      SetServerName(DoConnectDialog("Authentication failed - try again."));
    }
  }

  /* Create the "popup" widget - this won't actually appear on the screen until
     some user-defined event causes the "ShowPopup" action to be invoked */

  CreatePopup();

  /* Find the best pixel format and X visual/colormap to use */

  SetVisualAndCmap();

  /* Create the "desktop" widget, and perform initialisation which needs doing
     before the widgets are realized */

  ToplevelInitBeforeRealization();

  DesktopInitBeforeRealization();

  /* "Realize" all the widgets, i.e. actually create and map their X windows */

  XtRealizeWidget(toplevel);

  /* Perform initialisation that needs doing after realization, now that the X
     windows exist */

  InitialiseSelection();

  ToplevelInitAfterRealization();

  DesktopInitAfterRealization();

  /* Watch for window resizes so we can ask the server to resize its
     framebuffer to match (TigerVNC SetDesktopSize) */

  TrackDesktopResizes();

  /* Tell the VNC server which pixel format and encodings we want to use */

  SetFormatAndEncodings();

  /* Now enter the main loop, processing VNC messages.  X events will
     automatically be processed whenever the VNC connection is idle. */

  while (1) {
    ProcessPendingXEvents();
    if (!HandleRFBServerMessage())
      break;
  }

  Cleanup();

  return 0;
}
