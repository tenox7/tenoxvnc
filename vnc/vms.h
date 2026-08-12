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
 * DECwindows supplies Xlib, Xt R5, Xmu and Xext, but no Athena widget set
 * at all, so the parts of the viewer made of Xaw widgets are left out of
 * the VMS build: the F8 popup menu (popup.c), full-screen mode and bump
 * scrolling (fullscreen.c).  The server and password dialogs become
 * terminal prompts, and the scrolling an Xaw Viewport would have done is
 * done by vms.c.
 */

#ifndef VMS_H
#define VMS_H

/*
 * Xmu itself is present as SYS$SHARE:DECW$XMULIBSHRR5.EXE and both entry
 * points we need resolve against it, but DECwindows ships no Xmu headers,
 * so declare what <X11/Xmu/Converters.h> and <X11/Xmu/StdSel.h> would have.
 */

/*
 * /NAMES=UPPERCASE folds the Pause() action in misc.c onto the same external
 * name as the CRTL's pause(), so give ours a name of its own.  The "Pause"
 * action string in the table in argsresources.c is a string literal, so it is
 * untouched and translations keep working.
 */

#define Pause VncPause

#define XtNbackingStore "backingStore"
#define XtCBackingStore "BackingStore"
#define XtRBackingStore "BackingStore"

extern void XmuCvtStringToBackingStore(XrmValue *args, Cardinal *num_args,
				       XrmValue *from, XrmValue *to);

extern Boolean XmuConvertStandardSelection(Widget w, Time timestamp,
					   Atom *selection, Atom *target,
					   Atom *type, XPointer *value,
					   unsigned long *length, int *format);

/* vms.c */

extern void VmsScrollInit(void);
extern void VmsScrollResize(void);
extern Bool VmsSocketReady(int sock, int msec);
extern char *VmsPrompt(const char *prompt, Bool echo);

#endif /* VMS_H */
