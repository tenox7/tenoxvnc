# tenoxvnc - ultra portable vintage unix VNC viewer
# TightVNC 1.3.10 X11 viewer + TigerVNC feature backports, vendored zlib/jpeg
#
# Build:  make <target>   where target is one of:
#   linux solaris hpux hpux10 aix unixware osf1 irix irix5 netbsd macos
# or just "make" with default CC/CFLAGS/LDFLAGS below.

CC = gcc
INCS = -Ivnc -Izlib -Ijpeg
CFLAGS = -O2 $(INCS) -DMITSHM
LDFLAGS = -lXaw -lXmu -lXt -lXext -lX11 -lm
TARGET = tenoxvnc

VIEWER_SRCS = vnc/argsresources.c vnc/caps.c vnc/colour.c vnc/cursor.c \
	vnc/desktop.c vnc/dialogs.c vnc/fullscreen.c vnc/listen.c vnc/misc.c \
	vnc/popup.c vnc/rfbproto.c vnc/selection.c vnc/shm.c vnc/sockets.c \
	vnc/tunnel.c vnc/vncviewer.c vnc/vncauth.c vnc/d3des.c

# corre.c hextile.c rre.c tight.c zlib.c zrle.c are #included by rfbproto.c

ZLIB_OBJS = zlib/adler32.o zlib/crc32.o zlib/inflate.o zlib/inffast.o \
	zlib/inftrees.o zlib/zutil.o

JPEG_OBJS = jpeg/jcomapi.o jpeg/jutils.o jpeg/jerror.o jpeg/jmemmgr.o \
	jpeg/jmemnobs.o jpeg/jdapimin.o jpeg/jdapistd.o jpeg/jdatasrc.o \
	jpeg/jdcoefct.o jpeg/jdcolor.o jpeg/jddctmgr.o jpeg/jdhuff.o \
	jpeg/jdinput.o jpeg/jdmainct.o jpeg/jdmarker.o jpeg/jdmaster.o \
	jpeg/jdmerge.o jpeg/jdphuff.o jpeg/jdpostct.o jpeg/jdsample.o \
	jpeg/jdtrans.o jpeg/jquant1.o jpeg/jquant2.o jpeg/jidctflt.o \
	jpeg/jidctfst.o jpeg/jidctint.o jpeg/jidctred.o

OBJECTS = $(VIEWER_SRCS:.c=.o) $(ZLIB_OBJS) $(JPEG_OBJS)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

linux:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm"

macos:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/opt/X11/include" \
	  LDFLAGS="-L/opt/X11/lib -lXaw -lXmu -lXt -lXext -lX11 -lm"

solaris:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/openwin/include" \
	  LDFLAGS="-L/usr/openwin/lib -R/usr/openwin/lib -lXaw -lXmu -lXt -lXext -lX11 -lm -lsocket -lnsl"

hpux:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/include/X11R6" \
	  LDFLAGS="-L/usr/lib/X11R6 -lXaw -lXmu -lXt -lXext -lX11 -lm"

hpux10:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -DNO_SOCKLEN_T -I/usr/include/X11R6" \
	  LDFLAGS="-L/usr/lib/X11R6 -lXaw -lXmu -lXt -lXext -lX11 -lm"

aix:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm"

unixware:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm -lsocket -lnsl"

osf1:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm"

irix:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -isystem /usr/include" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm"

# see sng Makefile.x11 for the irix5 native-ld story; same recipe works here
irix5:
	$(MAKE) CFLAGS="-O2 $(INCS) -DNO_SOCKLEN_T -isystem /usr/include" \
	  LDFLAGS="-lXaw -lXmu -lXt -lXext -lX11 -lm"

netbsd:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/X11R7/include" \
	  LDFLAGS="-L/usr/X11R7/lib -R/usr/X11R7/lib -lXaw -lXmu -lXt -lXext -lX11 -lm"

clean:
	rm -f vnc/*.o zlib/*.o jpeg/*.o $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	-cp tenoxvnc.man /usr/local/man/man1/tenoxvnc.1

.PHONY: all clean install linux macos solaris hpux hpux10 aix unixware osf1 irix irix5 netbsd
