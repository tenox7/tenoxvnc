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
 * vms.h - OpenVMS declarations, included from vncviewer.h.
 *
 * Only the C runtime differences are left here.  The viewer no longer uses
 * Xaw or Xmu, so VMS needs nothing special for the user interface any more:
 * DECwindows supplies the Xlib and Xt that everything is now built on.
 */

#ifndef VMS_H
#define VMS_H

/*
 * /NAMES=UPPERCASE folds the Pause() action in misc.c onto the same external
 * name as the CRTL's pause(), so give ours a name of its own.  The "Pause"
 * action string in the table in argsresources.c is a string literal, so it is
 * untouched and translations keep working.
 */

#define Pause VncPause

/* vms.c */

extern Bool VmsSocketReady(int sock, int msec);
extern char *VmsPrompt(const char *prompt, Bool echo);

#endif /* VMS_H */
