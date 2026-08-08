# tenoxvnc - ultra portable vintage unix VNC viewer
# TightVNC 1.3.10 X11 viewer + TigerVNC feature backports, vendored zlib/jpeg
#
# Build:  make <target>   where target is one of:
#   linux solaris hpux hpux9 aix unixware osf1 irix irix5 netbsd macos
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

# POSIX suffix rule, not a GNU "%.o: %.c" pattern rule: HP-UX native make
# ignores pattern rules and its built-in .c.o has no -o $@, so objects land in
# the cwd instead of next to the source and the link fails on missing vnc/*.o
.SUFFIXES: .c .o

.c.o:
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

# hpux covers 10.20 and 11.x with native ANSI cc; needs gmake.
# Xaw/Xmu are static archives in /usr/contrib, hence explicit -lSM -lICE.
# hpux9 uses X11R5 which has no XShm headers.
hpux:
	$(MAKE) CC=cc CFLAGS="-Ae -O $(INCS) -DMITSHM -I/usr/include/X11R6 -I/usr/contrib/X11R6/include" \
	  LDFLAGS="-L/usr/lib/X11R6 -L/usr/contrib/X11R6/lib -lXaw -lXmu -lXt -lSM -lICE -lXext -lX11 -lm"

hpux9:
	$(MAKE) CC=cc CFLAGS="-Ae -O $(INCS) -I/usr/include/X11R5 -I/usr/contrib/X11R5/include" \
	  LDFLAGS="-L/usr/lib/X11R5 -L/usr/contrib/X11R5/lib -lXaw -lXmu -lXt -lXext -lX11 -lm"

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

# see sng Makefile.x11 for the irix5 native-ld story; same recipe works here:
# compile with gcc, link with tgcware GNU ld directly (5.3 native ld can't
# read gas objects, and collect2 has /usr/bin/ld baked in)
IRIX5_GCCLIB = /usr/tgcware/gcc45/lib/gcc/mips-sgi-irix5.3/4.5.3
IRIX5_LD = /usr/tgcware/mips-sgi-irix5.3/bin/ld

irix5:
	$(MAKE) $(OBJECTS) CFLAGS="-O2 $(INCS) -isystem /usr/include"
	$(IRIX5_LD) -o $(TARGET) -init __gcc_init -fini __gcc_fini \
	  /usr/lib/crt1.o $(IRIX5_GCCLIB)/irix-crti.o $(IRIX5_GCCLIB)/crtbegin.o \
	  -L$(IRIX5_GCCLIB) -L$(IRIX5_GCCLIB)/../../.. -L/usr/lib \
	  $(OBJECTS) -lXaw -lXmu -lXt -lXext -lX11 -lm -lgcc -lgcc_eh -lc \
	  $(IRIX5_GCCLIB)/crtend.o $(IRIX5_GCCLIB)/irix-crtn.o /usr/lib/crtn.o

netbsd:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/X11R7/include" \
	  LDFLAGS="-L/usr/X11R7/lib -R/usr/X11R7/lib -lXaw -lXmu -lXt -lXext -lX11 -lm"

clean:
	rm -f *.o */*.o $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	-cp tenoxvnc.man /usr/local/man/man1/tenoxvnc.1

.PHONY: all clean install linux macos solaris hpux hpux9 aix unixware osf1 irix irix5 netbsd
