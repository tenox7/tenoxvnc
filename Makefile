

CC = gcc
INCS = -I.
CFLAGS = -O2 $(INCS) -DMITSHM $(STATS)
LDFLAGS = -lXt -lXext -lX11 -lm
TARGET = tenoxvnc
# Diag under F8 Menu. Off unless VNCSTATS=true make <target>
STATS = $(VNCSTATS:true=-DVNCSTATS)

VIEWER_SRCS = argsresources.c caps.c color.c cursor.c desktop.c dialogs.c \
	fullscreen.c listen.c misc.c popup.c rfbproto.c selection.c shm.c \
	sockets.c stats.c tunnel.c vncviewer.c vncauth.c d3des.c xwidgets.c \
	scroll.c

# corre.c hextile.c rre.c tight.c zlibenc.c zrle.c are #included by rfbproto.c

CODEC_OBJS = zlib.o jpeg.o

OBJECTS = $(VIEWER_SRCS:.c=.o) $(CODEC_OBJS)

# Auto detect OS
all:
	@s=`uname -s`; r=`uname -r`; v=`uname -v`; m=`uname -m`; \
	case "$$s" in \
	  Linux)   t=linux;; \
	  Darwin)  t=macos;; \
	  SunOS)   case "$$r" in 4.*) t=sunos4;; *) t=solaris;; esac;; \
	  AIX)     t=aix;; \
	  OSF1)    t=osf1;; \
	  NetBSD)  t=netbsd;; \
	  HP-UX)   case "$$r" in \
	             *.09.*) t=hpux9;; \
	             *.10.*) t=hpux10;; \
	             *.11.31*) case "$$m" in ia64) t=hpux1131ia64;; *) t=hpux1131;; esac;; \
	             *) t=hpux11;; \
	           esac;; \
	  IRIX*)   case "$$r" in 5.*) t=irix53;; *) t=irix65;; esac;; \
	  UnixWare|UNIX_SV) t=unixware;; \
	  SCO_SV)  case "$$v" in 6.*) t=osr6;; *) t=osr5;; esac;; \
	  *) echo "$$s not known here, building with the defaults"; t=$(TARGET);; \
	esac; \
	echo "=> make $$t"; \
	$(MAKE) $$t

# Only hosts with git stamp the version; SVR make chokes on a phony prereq
VERSTAMP =

$(TARGET): $(VERSTAMP) $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# version.h gets the latest git tag; no-op without git
version:
	@v=`git describe --tags --abbrev=0 2>/dev/null`; test -n "$$v" || exit 0; \
	sed 's/TENOXVNC_VERSION ".*"/TENOXVNC_VERSION "'"$$v"'"/' \
	  version.h > version.tmp; \
	cmp -s version.tmp version.h || \
	  { echo "=> version $$v"; cp version.tmp version.h; }; \
	rm -f version.tmp

# POSIX suffix rule, not a GNU "%.o: %.c" pattern rule: HP-UX native make
# ignores pattern rules and its built-in .c.o has no -o $@
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

linux:
	$(MAKE) VERSTAMP=version CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

macos:
	$(MAKE) VERSTAMP=version CFLAGS="-O2 $(INCS) -DMITSHM -I/opt/X11/include $(STATS)" \
	  LDFLAGS="-L/opt/X11/lib -lXt -lXext -lX11 -lm" $(TARGET)

solaris:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/openwin/include $(STATS)" \
	  LDFLAGS="-L/usr/openwin/lib -R/usr/openwin/lib -lXt -lXext -lX11 -lm -lsocket -lnsl" $(TARGET)

# OpenWindows 3 is X11R4. Sockets are in libc and ld records -L for ld.so.
sunos4:
	$(MAKE) CC=gcc CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/openwin/include $(STATS)" \
	  LDFLAGS="-L/usr/openwin/lib -lXt -lXext -lX11 -lm" $(TARGET)

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

# 11.31 on PA-RISC 2.0. Unlike 11i v1 this one does ship libFOO.sl aliases, so
# plain -l works; no gcc here, the bundled ANSI cc is /opt/ansic/bin/cc.
hpux1131:
	$(MAKE) CC=/opt/ansic/bin/cc \
	  CFLAGS="-Ae +O3 +DA2.0 +DS2.0 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-L/usr/lib/X11R6 -lXt -lSM -lICE -lXext -lX11 -lm" $(TARGET)

# 11.31 on Itanium; 11.31 also runs on PA-RISC, hence both are matched above.
# /usr/lib and /usr/lib/X11R6 are PA-RISC SOM: "Mismatched ABI (not an ELF
# file)". The ELF libs are in /usr/lib/hpux32, the aCC default. No gcc here.
hpux1131ia64:
	$(MAKE) CC=/opt/ansic/bin/aCC \
	  CFLAGS="-Ae +O3 +DSitanium2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-L/usr/lib/hpux32 -lXt -lSM -lICE -lXext -lX11 -lm" $(TARGET)

aix:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

# libXt.so pulls in the Smc*/Ice* session-management calls but does not record
# a dependency on them, so -lSM -lICE have to be named explicitly.
unixware:
	$(MAKE) CC=/usr/ccs/bin/cc CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lSM -lICE -lXext -lX11 -lm -lsocket -lnsl" $(TARGET)

# OpenServer 5 and 6 both report uname -s SCO_SV, only uname -v distinguishes
# them (5.0.7 vs 6.0.0). On 6 the X11 tree moved to /usr/X11R6.
osr5:
	$(MAKE) CC=/udk/usr/ccs/bin/cc CFLAGS="-O $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lm" $(TARGET)

osr6:
	$(MAKE) CC=/udk/usr/ccs/bin/cc CFLAGS="-O -I/usr/X11R6/include $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-L/usr/X11R6/lib -lXt -lSM -lICE -lXext -lX11 -lsocket -lnsl -lm" $(TARGET)

osf1:
	$(MAKE) CC=cc CFLAGS="-O2 $(INCS) -DMITSHM $(STATS)" \
	  LDFLAGS="-lXt -lXext -lX11 -lm" $(TARGET)

# MIPSpro cc, -woff mutes unused-variable warnings. mips3 runs on any 6.5 box,
# mips4 needs R5000 or newer. MIPS V exists on paper only, MIPSpro stops at 4.
irix65:
	$(MAKE) CC=cc CFLAGS="-n32 -mips3 -O2 $(INCS) -DMITSHM -woff 1174,1552 $(STATS)" \
	  LDFLAGS="-n32 -mips3 -lXt -lXext -lX11 -lm" $(TARGET)

irix65mips4:
	$(MAKE) CC=cc CFLAGS="-n32 -mips4 -O2 $(INCS) -DMITSHM -woff 1174,1552 $(STATS)" \
	  LDFLAGS="-n32 -mips4 -lXt -lXext -lX11 -lm" $(TARGET)

IRIX5_GCCLIB = /usr/tgcware/gcc45/lib/gcc/mips-sgi-irix5.3/4.5.3
IRIX5_LD = /usr/tgcware/mips-sgi-irix5.3/bin/ld

# gcc fakes _COMPILER_VERSION, so SGI's offsetof() needs MIPSpro's __INTADDR__
irix53:
	$(MAKE) $(OBJECTS) CFLAGS="-O2 $(INCS) -isystem /usr/include -U_COMPILER_VERSION $(STATS)"
	$(IRIX5_LD) -o $(TARGET) -init __gcc_init -fini __gcc_fini \
	  /usr/lib/crt1.o $(IRIX5_GCCLIB)/irix-crti.o $(IRIX5_GCCLIB)/crtbegin.o \
	  -L$(IRIX5_GCCLIB) -L$(IRIX5_GCCLIB)/../../.. -L/usr/lib \
	  $(OBJECTS) -lXt -lXext -lX11 -lm -lgcc -lgcc_eh -lc \
	  $(IRIX5_GCCLIB)/crtend.o $(IRIX5_GCCLIB)/irix-crtn.o /usr/lib/crtn.o

netbsd:
	$(MAKE) CFLAGS="-O2 $(INCS) -DMITSHM -I/usr/X11R7/include $(STATS)" \
	  LDFLAGS="-L/usr/X11R7/lib -R/usr/X11R7/lib -lXt -lXext -lX11 -lm" $(TARGET)

clean:
	rm -f *.o $(TARGET)

install: all
	cp $(TARGET) /usr/local/bin/
	-cp tenoxvnc.man /usr/local/man/man1/tenoxvnc.1

.PHONY: all version clean install linux macos solaris sunos4 hpux9 hpux10 hpux11 hpux1131 hpux1131ia64 aix unixware osr5 osr6 osf1 irix53 irix65 irix65mips4 netbsd
