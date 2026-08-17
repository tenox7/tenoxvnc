/*
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
 * vms.c - the OpenVMS C runtime differences.
 *
 * The scrolling this file used to carry is in scroll.c now, used on every
 * platform.  What is left is the two things the VAX C runtime cannot do:
 * wait on a socket without XtAppAddInput (which DECwindows Xt never fires
 * for a socket), and read a password from the terminal, since getpass() does
 * not exist and access-violates if called.
 */

#include <vncviewer.h>
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <starlet.h>


/*
 * VmsSocketReady waits up to msec milliseconds for the socket to become
 * readable.  select() on VMS works for sockets but not for a bare timeout
 * with no descriptors, which is what Msleep() would want.
 */

Bool
VmsSocketReady(int sock, int msec)
{
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  tv.tv_sec = msec / 1000;
  tv.tv_usec = (msec % 1000) * 1000;

  return select(sock + 1, &fds, NULL, NULL, &tv) > 0;
}


/*
 * VmsPrompt reads a line from the terminal, optionally without echoing it.
 * Replaces getpass(), which the VAX CRTL does not have.  Returns a pointer
 * to a static, writable buffer - callers truncate and wipe it in place.
 *
 * Mostly unused now that the connection dialog asks for the password, but
 * still the fallback when SYS$COMMAND is not a terminal.
 */

char *
VmsPrompt(const char *prompt, Bool echo)
{
  static char buf[256];
  $DESCRIPTOR(devDsc, "SYS$COMMAND");
  struct {
    unsigned short status;
    unsigned short count;
    unsigned int info;
  } iosb;
  unsigned short chan;
  int status, len;

  buf[0] = '\0';

  /* A no-echo read needs the terminal driver; if SYS$COMMAND is not a
     terminal (a detached process, or a pipe) fall back to stdio. */

  if (!echo) {
    status = sys$assign(&devDsc, &chan, 0, 0);
    if (status & 1) {
      status = sys$qiow(0, chan, IO$_READPROMPT | IO$M_NOECHO | IO$M_PURGE,
			&iosb, 0, 0, buf, sizeof(buf) - 1, 0, 0,
			(char *)prompt, strlen(prompt));
      sys$dassgn(chan);

      if ((status & 1) && (iosb.status & 1)) {
	buf[iosb.count] = '\0';
	fprintf(stderr, "\n");
	return buf;
      }
    }
  }

  fprintf(stderr, "%s", prompt);
  fflush(stderr);

  if (!fgets(buf, sizeof(buf), stdin))
    return NULL;

  len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    buf[--len] = '\0';

  return buf;
}
