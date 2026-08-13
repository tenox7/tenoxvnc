

CC = gcc
INCS = -Ivnc -Izlib -Ijpeg
CFLAGS = -O2 $(INCS) -DMITSHM $(STATS)
LDFLAGS = -lXt -lXext -lX11 -lm
TARGET = tenoxvnc
# Diag under F8 Menu. Off unless VNCSTATS=true make <target>
STATS = $(VNCSTATS:true=-DVNCSTATS)

VIEWER_SRCS = vnc/argsresources.c vnc/caps.c vnc/colour.c vnc/cursor.c \
	vnc/desktop.c vnc/dialogs.c vnc/fullscreen.c vnc/listen.c vnc/misc.c \
	vnc/popup.c vnc/rfbproto.c vnc/selection.c vnc/shm.c vnc/sockets.c \
	vnc/stats.c vnc/tunnel.c vnc/vncviewer.c vnc/vncauth.c vnc/d3des.c \
	vnc/xwidgets.c vnc/scroll.c

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

# Auto detect OS
all:
	@s=`uname -s`; r=`uname -r`; \
	case "$$s" in \
	  Linux)   t=linux;; \
	  Darwin)  t=macos;; \
	  SunOS)   t=solaris;; \
	  AIX)     t=aix;; \
	  OSF1)    t=osf1;; \
	  NetBSD)  t=netbsd;; \
	  HP-UX)   case "$$r" in *.09.*) t=hpux9;; *.10.*) t=hpux10;; *) t=hpux11;; esac;; \
	  IRIX*)   case "$$r" in 5.*) t=irix5;; *) t=irix;; esac;; \
	  UnixWare|UNIX_SV|SCO_SV) t=unixware;; \
	  *) echo "$$s not known here, building with the defaults"; t=$(TARGET);; \
	esac; \
	echo "=> make $$t"; \
	$(MAKE) $$t

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# POSIX suffix rule, not a GNU "%.o: %.c" pattern rule: HP-UX native make
# ignores pattern rules and its built-in .c.o has no -o $@, so objects land in
# the cwd instead of next to the source and the link fails on missing vnc/*.o
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

linux:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

macos:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/opt/X11/include $(STATS)" \
	  LDFLAGS="-L/opt/X11/lib -lXt -lXext -lX11 -lm" $(TARGET)

solaris:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/openwin/include $(STATS)" \
	  LDFLAGS="-L/usr/openwin/lib -R/usr/openwin/lib -lXt -lXext -lX11 -lm -lsocket -lnsl" $(TARGET)

# All HP-UX targets need gmake. 9 and 10 use the native ANSI cc, 11 uses gcc.
# hpux9 uses X11R5 which has no XShm headers.
hpux9:
	$(MAKE) CC=cc CFLAGS="-Ae -O $(INCS) -I/usr/include/X11R5 -I/usr/contrib/X11R5/include $(STATS)" \
	  LDFLAGS="-L/usr/lib/X11R5 -L/usr/contrib/X11R5/lib -lXt -lXext -lX11 -lm" $(TARGET)

# On 10.20 Xt is a static archive in /usr/contrib, hence explicit -lSM -lICE.
hpux10:
	$(MAKE) CC=cc CFLAGS="-Ae -O $(INCS) -DMITSHM -I/usr/include/X11R6 -I/usr/contrib/X11R6/include $(STATS)" \
	  LDFLAGS="-L/usr/lib/X11R6 -L/usr/contrib/X11R6/lib -lXt -lSM -lICE -lXext -lX11 -lm" $(TARGET)

# 11i keeps all X11 headers in /usr/include/X11 and ships the shared libs only
# as /usr/lib/X11R6/libFOO.<N> — there are no libFOO.sl aliases, so ld cannot
# resolve -lXt and the libraries have to be named by full path.
HPUX11_X11 = /usr/lib/X11R6
HPUX11_XLIBS = $(HPUX11_X11)/libXt.3 $(HPUX11_X11)/libSM.2 $(HPUX11_X11)/libICE.2 \
  $(HPUX11_X11)/libXext.3 $(HPUX11_X11)/libX11.3

hpux11:
	$(MAKE) CC=gcc CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="$(HPUX11_XLIBS) -lm" $(TARGET)

aix:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

unixware:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm -lsocket -lnsl" $(TARGET)

osf1:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

irix:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -isystem /usr/include $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

IRIX5_GCCLIB = /usr/tgcware/gcc45/lib/gcc/mips-sgi-irix5.3/4.5.3
IRIX5_LD = /usr/tgcware/mips-sgi-irix5.3/bin/ld

irix5:
	$(MAKE) $(OBJECTS) CFLAGS="-O2 $(INCS) -isystem /usr/include $(STATS)"
	$(IRIX5_LD) -o $(TARGET) -init __gcc_init -fini __gcc_fini \
	  /usr/lib/crt1.o $(IRIX5_GCCLIB)/irix-crti.o $(IRIX5_GCCLIB)/crtbegin.o \
	  -L$(IRIX5_GCCLIB) -L$(IRIX5_GCCLIB)/../../.. -L/usr/lib \
	  $(OBJECTS) -lXt -lXext -lX11 -lm -lgcc -lgcc_eh -lc \
	  $(IRIX5_GCCLIB)/crtend.o $(IRIX5_GCCLIB)/irix-crtn.o /usr/lib/crtn.o

netbsd:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/X11R7/include $(STATS)" \
	  LDFLAGS="-L/usr/X11R7/lib -R/usr/X11R7/lib -lXt -lXext -lX11 -lm" $(TARGET)

clean:
	rm -f *.o */*.o $(TARGET)

install: all
	cp $(TARGET) /usr/local/bin/
	-cp tenoxvnc.man /usr/local/man/man1/tenoxvnc.1

.PHONY: all clean install linux macos solaris hpux9 hpux10 hpux11 aix unixware osf1 irix irix5 netbsd
