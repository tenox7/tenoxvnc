# win32\common.mak - sources and rules shared by Makefile.w35 and Makefile.w40

TARGET = tenoxvnc.exe

INCS = -I. -Iwin32
CFLAGS = $(cflags) $(cvars) $(cdebug) -nologo -DWIN32_LEAN_AND_MEAN $(INCS)
LIBS = $(libc) kernel32.lib user32.lib gdi32.lib wsock32.lib

# corre.c hextile.c rre.c tight.c zlibenc.c zrle.c are #included by rfbproto.c
OBJS = vncviewer.obj argsresources.obj rfbproto.obj sockets.obj misc.obj \
	caps.obj vncauth.obj d3des.obj zlib.obj jpeg.obj \
	w32xt.obj w32desk.obj w32color.obj w32curs.obj w32key.obj \
	w32dlg.obj w32misc.obj

all: $(TARGET)

$(TARGET): $(OBJS) tenoxvnc.res
	$(link) $(LINKFLAGS) -out:$(TARGET) $(OBJS) tenoxvnc.res $(LIBS)

.c.obj:
	$(cc) $(CFLAGS) $<

{win32}.c{}.obj:
	$(cc) $(CFLAGS) $<

rfbproto.obj: rfbproto.c rre.c corre.c hextile.c zlibenc.c tight.c zrle.c

# chmod() is in io.h, and the Unix permission names are not there at all
vncauth.obj: vncauth.c
	$(cc) $(CFLAGS) -FIio.h -DS_IRUSR=0400 -DS_IWUSR=0200 vncauth.c

tenoxvnc.res: win32\tenoxvnc.rc win32\w32res.h
	$(rc) $(rcvars) -r -iwin32 -fo tenoxvnc.res win32\tenoxvnc.rc

clean:
	-del *.obj
	-del tenoxvnc.res
	-del $(TARGET)
