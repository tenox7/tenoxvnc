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
 * shm.c - code to set up shared memory extension.
 */

#include <vncviewer.h>

#ifdef MITSHM

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
static XShmSegmentInfo shminfo;

static Bool caughtShmError = False;
static Bool needShmCleanup = False;
static Bool shmAnnounced = False;

void
ShmCleanup()
{
  if (needShmCleanup) {
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, 0);
    needShmCleanup = False;
  }
}

/*
 * ShmDetachImage - detach and destroy a shared memory image, e.g. before
 * reallocating it at a new framebuffer size.
 */

void
ShmDetachImage(XImage *img)
{
  if (!needShmCleanup)
    return;

  XShmDetach(dpy, &shminfo);
  XSync(dpy, False);
  img->data = NULL;		/* shm memory, must not be free()d */
  XDestroyImage(img);
  ShmCleanup();
}

static int
ShmCreationXErrorHandler(Display *dpy, XErrorEvent *error)
{
  caughtShmError = True;
  return 0;
}

XImage *
CreateShmImage()
{
  XImage *image;
  XErrorHandler oldXErrorHandler;

  if (!XShmQueryExtension(dpy))
    return NULL;

  image = XShmCreateImage(dpy, vis, visdepth, ZPixmap, NULL, &shminfo,
			  si.framebufferWidth, si.framebufferHeight);
  if (!image) return NULL;

  shminfo.shmid = shmget(IPC_PRIVATE,
			 image->bytes_per_line * image->height,
			 IPC_CREAT|0777);

  if (shminfo.shmid == -1) {
    XDestroyImage(image);
    return NULL;
  }

  shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);

  if (shminfo.shmaddr == (char *)-1) {
    XDestroyImage(image);
    shmctl(shminfo.shmid, IPC_RMID, 0);
    return NULL;
  }

  shminfo.readOnly = True;

  oldXErrorHandler = XSetErrorHandler(ShmCreationXErrorHandler);
  XShmAttach(dpy, &shminfo);
  XSync(dpy, False);
  XSetErrorHandler(oldXErrorHandler);

  if (caughtShmError) {
    XDestroyImage(image);
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, 0);
    return NULL;
  }

  needShmCleanup = True;

  if (!shmAnnounced) {
    fprintf(stderr,"Using shared memory PutImage\n");
    shmAnnounced = True;
  }

  return image;
}

#endif /* MITSHM */
