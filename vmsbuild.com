$! TenoxVNC build procedure for OpenVMS (DECwindows X11)
$!
$! Tested on VAX/VMS V7.3 with Compaq C V6.4 and DECwindows Motif.
$!
$! DECwindows has no Athena widget set, and the viewer no longer wants one:
$! it draws its own dialog, F8 menu and scrollbars with Xlib, so this links
$! against nothing but Xlib, Xt and Xext.  MIT-SHM is not defined either -
$! there is no System V shared memory on VMS.
$!
$! The sources are copied to a work directory on a local disk and built
$! there, then only the finished image is copied back.  Building directly in
$! an NFS-mounted source tree is not reliable: CC occasionally reads a source
$! over NFS, exits reporting success and writes no object at all, and objects
$! it has just written are not always visible to the next image that opens
$! them.  Working locally also keeps the source tree clean - CC and LINK both
$! drop a CXX_REPOSITORY directory into wherever they run.
$!
$! Usage: @VMSBUILD          build
$!        @VMSBUILD STATS    build with the F8 diagnostics window
$!        @VMSBUILD CLEAN    remove the work directory and the image
$!
$ ON ERROR THEN GOTO ERROR
$ SAY := WRITE SYS$OUTPUT
$! Run from the procedure's own directory (rsh lands in SYS$LOGIN)
$ PROC = F$ENVIRONMENT("PROCEDURE")
$ SET DEFAULT 'F$PARSE(PROC,,,"DEVICE","SYNTAX_ONLY")''F$PARSE(PROC,,,"DIRECTORY","SYNTAX_ONLY")'
$ SRC = F$ENVIRONMENT("DEFAULT")
$ VNCDIR = SRC - "]" + ".VNC]"
$ ZLIBDIR = SRC - "]" + ".ZLIB]"
$ JPEGDIR = SRC - "]" + ".JPEG]"
$!
$! The work directory has to be a concrete path.  SYS$SCRATCH translates to
$! SYS$SYSROOT:, which is a search list (SYS$SPECIFIC:,SYS$COMMON:) - a
$! directory created through it exists in only one element and lookups that
$! land on the other fail with RMS-E-DNF.  SYS$SPECIFIC is a real device.
$! Define TENOXVNC_WORK to build somewhere else.
$ WORK = F$TRNLNM("TENOXVNC_WORK")
$ IF WORK .EQS. "" THEN WORK = F$TRNLNM("SYS$SPECIFIC") - "]" + "TENOXVNC_BLD]"
$ REPO = WORK - "]" + ".CXX_REPOSITORY]"
$!
$ IF P1 .EQS. "CLEAN" THEN GOTO CLEAN
$!
$! Make #include <X11/...> resolve to the DECwindows headers
$ IF F$TRNLNM("X11") .EQS. "" THEN DEFINE/NOLOG X11 DECW$INCLUDE
$!
$ IF F$PARSE(WORK) .EQS. "" THEN CREATE/DIRECTORY 'WORK'
$ SAY "Work directory: ", WORK
$ GOSUB PURGEWORK
$!
$! vnc, zlib and jpeg have no filenames in common, so they can share one
$! directory.  Flattening them keeps #include "zlib.c" (vnc) and
$! #include <zlib.h> (zlib) resolving to the right files, and keeps the
$! compile command short - see the note on CFLAGS below.
$ SAY "Copying sources..."
$ COPY 'VNCDIR'*.C;*,'VNCDIR'*.H;* 'WORK'*
$ COPY 'ZLIBDIR'*.C;*,'ZLIBDIR'*.H;* 'WORK'*
$ COPY 'JPEGDIR'*.C;*,'JPEGDIR'*.H;* 'WORK'*
$ COPY 'SRC'TENOXVNC.OPT 'WORK'*
$!
$ SET DEFAULT 'WORK'
$!
$ DEFS = ""
$ IF P1 .EQS. "STATS" THEN DEFS = "/DEFINE=(VNCSTATS)"
$! Keep this short.  DCL truncates a command over 255 characters with TKNOVF,
$! after which CC writes no object - which only shows up later as LIBRARIAN
$! failing to open it.  Everything is in the current directory now, so the
$! longest compile line is about 190 characters.
$ CFLAGS = DEFS + -
           "/INCLUDE_DIRECTORY=([])" + -
           "/NESTED=INCLUDE_FILE" + -
           "/NAMES=(UPPERCASE,SHORTENED)" + -
           "/STANDARD=RELAXED" + -
           "/NOLIST" + -
           "/WARNINGS=DISABLE=(BADHEXCONST,PTRMISMATCH1)"
$!
$! /NOLIST is explicit because CC defaults to /LIST in a batch job, which
$! would drop 50-odd .LIS files alongside the objects.
$!
$! zlib's zutil.h tests ULONG_MAX against a 64-bit constant, and the viewer
$! passes char* to routines taking unsigned char*; neither matters here, so
$! those two warnings are off above to keep real diagnostics visible.
$!
$ LIBRARY/CREATE/OBJECT TENOXVNC.OLB
$!
$ SAY "Compiling viewer..."
$ LIST = "ARGSRESOURCES CAPS COLOUR CURSOR D3DES DESKTOP DIALOGS FULLSCREEN"
$ GOSUB COMPILE
$ LIST = "LISTEN MISC POPUP RFBPROTO SELECTION SHM SOCKETS STATS TUNNEL"
$ GOSUB COMPILE
$ LIST = "VNCAUTH VNCVIEWER VMS XWIDGETS SCROLL"
$ GOSUB COMPILE
$!
$ SAY "Compiling zlib..."
$ LIST = "ADLER32 CRC32 INFLATE INFFAST INFTREES ZUTIL"
$ GOSUB COMPILE
$!
$ SAY "Compiling jpeg..."
$ LIST = "JCOMAPI JUTILS JERROR JMEMMGR JMEMNOBS JDAPIMIN JDAPISTD JDATASRC"
$ GOSUB COMPILE
$ LIST = "JDCOEFCT JDCOLOR JDDCTMGR JDHUFF JDINPUT JDMAINCT JDMARKER JDMASTER"
$ GOSUB COMPILE
$ LIST = "JDMERGE JDPHUFF JDPOSTCT JDSAMPLE JDTRANS JQUANT1 JQUANT2"
$ GOSUB COMPILE
$ LIST = "JIDCTFLT JIDCTFST JIDCTINT JIDCTRED"
$ GOSUB COMPILE
$!
$ SAY "Linking..."
$! /NOMAP for the same reason as /NOLIST - LINK defaults to /MAP in batch
$ LINK/NOMAP/EXECUTABLE=TENOXVNC.EXE VNCVIEWER.OBJ, -
      TENOXVNC.OLB/LIBRARY, -
      TENOXVNC.OPT/OPTIONS
$ IF .NOT. $STATUS THEN GOTO ERROR
$!
$ SET DEFAULT 'SRC'
$ COPY 'WORK'TENOXVNC.EXE []TENOXVNC.EXE
$ IF .NOT. $STATUS THEN GOTO ERROR
$ GOSUB PURGEWORK
$!
$ SAY "Build complete: TENOXVNC.EXE"
$ SAY "Run with:  MCR SYS$DISK:[]TENOXVNC.EXE <host>::<port>"
$ EXIT
$!
$! Compile every file named in LIST, which is now in the current directory,
$! and add it to the library straight away.
$!
$! The object is checked for after each compile because CC has been seen to
$! report success having written nothing at all; without this the build fails
$! much later with a confusing LIBRARIAN error naming a different file.  One
$! retry is enough for the transient case.
$ COMPILE:
$ I = 0
$ CLOOP:
$   F = F$ELEMENT(I, " ", LIST)
$   IF F .EQS. " " THEN RETURN
$   SAY "  ", F
$   CC 'CFLAGS' 'F'.C
$   IF .NOT. $STATUS THEN GOTO ERROR
$   IF F$SEARCH(F + ".OBJ") .EQS. ""  THEN GOSUB RECOMPILE
$   IF F$SEARCH(F + ".OBJ") .EQS. ""  THEN GOTO NOOBJ
$   LIBRARY/REPLACE/OBJECT TENOXVNC.OLB 'F'.OBJ
$   IF .NOT. $STATUS THEN GOTO ERROR
$   I = I + 1
$   GOTO CLOOP
$!
$ RECOMPILE:
$ SAY "    no object produced, retrying ", F
$ CC 'CFLAGS' 'F'.C
$ RETURN
$!
$! Empty and remove the work directory contents, including the CXX_REPOSITORY
$! subdirectory CC creates for /NAMES=SHORTENED - a plain *.*;* cannot delete
$! a directory file that still has files in it.
$ PURGEWORK:
$ IF F$SEARCH(REPO + "*.*") .NES. "" THEN DELETE/NOCONFIRM 'REPO'*.*;*
$ IF F$SEARCH(WORK + "CXX_REPOSITORY.DIR") .NES. "" THEN -
     DELETE/NOCONFIRM 'WORK'CXX_REPOSITORY.DIR;*
$ IF F$SEARCH(WORK + "*.*") .NES. "" THEN DELETE/NOCONFIRM 'WORK'*.*;*
$ RETURN
$!
$ CLEAN:
$ GOSUB PURGEWORK
$ IF F$SEARCH("TENOXVNC.EXE") .NES. "" THEN DELETE/NOCONFIRM TENOXVNC.EXE;*
$! earlier versions of this procedure built in the source directory
$ IF F$SEARCH("*.OBJ") .NES. "" THEN DELETE/NOCONFIRM *.OBJ;*
$ IF F$SEARCH("*.LIS") .NES. "" THEN DELETE/NOCONFIRM *.LIS;*
$ IF F$SEARCH("*.MAP") .NES. "" THEN DELETE/NOCONFIRM *.MAP;*
$ IF F$SEARCH("BUILD.LOG") .NES. "" THEN DELETE/NOCONFIRM BUILD.LOG;*
$ IF F$SEARCH("TENOXVNC.OLB") .NES. "" THEN DELETE/NOCONFIRM TENOXVNC.OLB;*
$ SAY "Clean complete"
$ EXIT
$!
$ NOOBJ:
$ SAY "No object produced for ", F, " - CC reported success but wrote nothing,"
$ SAY "twice.  If DCL printed TKNOVF above, the compile command exceeded 255"
$ SAY "characters and was truncated; shorten CFLAGS."
$ EXIT %X10000004
$!
$ ERROR:
$ SAY "Build failed"
$ EXIT %X10000004
