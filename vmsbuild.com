$! TenoxVNC build procedure for OpenVMS (DECwindows X11)
$!
$! Tested on VAX/VMS V7.3 with Compaq C V6.4 and DECwindows Motif.
$!
$! DECwindows has no Athena widget set, so the F8 popup menu and full-screen
$! mode are left out, the server/password dialogs become terminal prompts,
$! and vnc/vms.c does the desktop scrolling an Xaw Viewport would have done.
$! MIT-SHM is not defined: there is no System V shared memory on VMS.
$!
$! Usage: @VMSBUILD          build
$!        @VMSBUILD STATS    build with the F8 diagnostics window
$!        @VMSBUILD CLEAN    remove objects, library and image
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
$ IF P1 .EQS. "CLEAN" THEN GOTO CLEAN
$!
$! Make #include <X11/...> resolve to the DECwindows headers
$ IF F$TRNLNM("X11") .EQS. "" THEN DEFINE/NOLOG X11 DECW$INCLUDE
$!
$! Build products go to a work directory on a local disk, not next to the
$! sources.  If the source tree is NFS-mounted, an object written by CC is not
$! reliably visible to the next image that opens it - the VMS NFS client caches
$! name lookups, so LIBRARIAN intermittently fails with OPENIN/FNF on a file
$! that was just compiled.  Keeping objects local avoids that entirely, and
$! leaves the source tree clean.
$!
$! The path has to be concrete: SYS$SCRATCH here translates to SYS$SYSROOT:,
$! which is a search list (SYS$SPECIFIC:,SYS$COMMON:).  A directory created
$! through it exists in only one element, and lookups that land on the other
$! fail with RMS-E-DNF.  SYS$SPECIFIC translates to a real device, so expand
$! that instead.  Define TENOXVNC_WORK to put the objects somewhere else.
$ WORK = F$TRNLNM("TENOXVNC_WORK")
$ IF WORK .EQS. "" THEN WORK = F$TRNLNM("SYS$SPECIFIC") - "]" + "TENOXVNC_BLD]"
$ IF F$PARSE(WORK) .EQS. "" THEN CREATE/DIRECTORY 'WORK'
$ SAY "Work directory: ", WORK
$ IF F$SEARCH(WORK + "*.*") .NES. "" THEN DELETE/NOCONFIRM 'WORK'*.*;*
$!
$! DCL truncates a command longer than 255 characters with TKNOVF, after which
$! CC quietly produces no object - which surfaces later as LIBRARIAN failing to
$! open it, on whichever file happened to push the line over the limit.  So the
$! include path and the object directory are reached through logical names
$! instead of being spelled out on every compile.  Keep CFLAGS short: the
$! longest compile line is about 220 characters as it stands.
$ DEFINE/NOLOG VNCINC 'VNCDIR','ZLIBDIR','JPEGDIR'
$ DEFINE/NOLOG VNCOBJ 'WORK'
$ DEFINE/NOLOG VNCSRC 'VNCDIR'
$ DEFINE/NOLOG ZLIBSRC 'ZLIBDIR'
$ DEFINE/NOLOG JPEGSRC 'JPEGDIR'
$ DEFS = ""
$ IF P1 .EQS. "STATS" THEN DEFS = "/DEFINE=(VNCSTATS)"
$ CFLAGS = DEFS + -
           "/INCLUDE_DIRECTORY=(VNCINC)" + -
           "/NESTED=INCLUDE_FILE" + -
           "/NAMES=(UPPERCASE,SHORTENED)" + -
           "/STANDARD=RELAXED" + -
           "/NOLIST" + -
           "/WARNINGS=DISABLE=(BADHEXCONST,PTRMISMATCH1)"
$!
$! /NOLIST is explicit because CC defaults to /LIST when it runs in a batch
$! job, which would drop 50-odd .LIS files into the work directory.
$!
$! zlib's zutil.h tests ULONG_MAX against a 64-bit constant, and the viewer
$! passes char* to routines taking unsigned char*; neither matters here, so
$! those two warnings are off above to keep real diagnostics visible.
$!
$! Compiled and linked from the work directory: /NAMES=SHORTENED makes CC write
$! a CXX_REPOSITORY demangler database into whatever directory it runs in, and
$! LINK does the same, so running there keeps the source tree clean.
$! (/REPOSITORY is a C++ qualifier - DEC C accepts and ignores it.)
$ SET DEFAULT VNCOBJ:
$ LIBRARY/CREATE/OBJECT TENOXVNC.OLB
$!
$ SAY "Compiling viewer..."
$ SRCDIR = "VNCSRC:"
$ LIST = "ARGSRESOURCES CAPS COLOUR CURSOR D3DES DESKTOP DIALOGS FULLSCREEN"
$ GOSUB COMPILE
$ LIST = "LISTEN MISC POPUP RFBPROTO SELECTION SHM SOCKETS STATS TUNNEL"
$ GOSUB COMPILE
$ LIST = "VNCAUTH VNCVIEWER VMS"
$ GOSUB COMPILE
$!
$ SAY "Compiling zlib..."
$ SRCDIR = "ZLIBSRC:"
$ LIST = "ADLER32 CRC32 INFLATE INFFAST INFTREES ZUTIL"
$ GOSUB COMPILE
$!
$ SAY "Compiling jpeg..."
$ SRCDIR = "JPEGSRC:"
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
$! Still in the work directory.  /NOMAP for the same reason as /NOLIST above -
$! LINK defaults to /MAP in batch.
$ LINK/NOMAP/EXECUTABLE=TENOXVNC.EXE VNCVIEWER.OBJ, -
      TENOXVNC.OLB/LIBRARY, -
      'SRC'TENOXVNC.OPT/OPTIONS
$ IF .NOT. $STATUS THEN GOTO ERROR
$ SET DEFAULT 'SRC'
$!
$! Only the finished image is copied back into the source tree
$ COPY VNCOBJ:TENOXVNC.EXE []TENOXVNC.EXE
$ IF .NOT. $STATUS THEN GOTO ERROR
$ DELETE/NOCONFIRM 'WORK'*.*;*
$!
$ SAY "Build complete: TENOXVNC.EXE"
$ SAY "Run with:  MCR SYS$DISK:[]TENOXVNC.EXE <host>::<port>"
$ EXIT
$!
$! Compile every file named in LIST out of SRCDIR into the work directory and
$! add it to the library straight away, so all three source directories end up
$! in one library.
$ COMPILE:
$ I = 0
$ CLOOP:
$   F = F$ELEMENT(I, " ", LIST)
$   IF F .EQS. " " THEN RETURN
$   SAY "  ", F
$   CC 'CFLAGS' 'SRCDIR''F'.C /OBJECT=VNCOBJ:'F'.OBJ
$   IF .NOT. $STATUS THEN GOTO ERROR
$   IF F$SEARCH("VNCOBJ:" + F + ".OBJ") .EQS. "" THEN GOTO NOOBJ
$   LIBRARY/REPLACE/OBJECT VNCOBJ:TENOXVNC.OLB VNCOBJ:'F'.OBJ
$   IF .NOT. $STATUS THEN GOTO ERROR
$   I = I + 1
$   GOTO CLOOP
$!
$ CLEAN:
$ WORK = F$TRNLNM("TENOXVNC_WORK")
$ IF WORK .EQS. "" THEN WORK = F$TRNLNM("SYS$SPECIFIC") - "]" + "TENOXVNC_BLD]"
$ IF F$SEARCH(WORK + "*.*") .NES. "" THEN DELETE/NOCONFIRM 'WORK'*.*;*
$! earlier versions of this procedure built in the source directory
$ IF F$SEARCH("*.OBJ") .NES. "" THEN DELETE/NOCONFIRM *.OBJ;*
$ IF F$SEARCH("*.LIS") .NES. "" THEN DELETE/NOCONFIRM *.LIS;*
$ IF F$SEARCH("*.MAP") .NES. "" THEN DELETE/NOCONFIRM *.MAP;*
$ IF F$SEARCH("BUILD.LOG") .NES. "" THEN DELETE/NOCONFIRM BUILD.LOG;*
$ IF F$SEARCH("[.CXX_REPOSITORY]*.*") .NES. "" THEN -
     DELETE/NOCONFIRM [.CXX_REPOSITORY]*.*;*
$ IF F$SEARCH("CXX_REPOSITORY.DIR") .NES. "" THEN -
     DELETE/NOCONFIRM CXX_REPOSITORY.DIR;*
$ IF F$SEARCH("TENOXVNC.OLB") .NES. "" THEN DELETE/NOCONFIRM TENOXVNC.OLB;*
$ IF F$SEARCH("TENOXVNC.EXE") .NES. "" THEN DELETE/NOCONFIRM TENOXVNC.EXE;*
$ SAY "Clean complete"
$ EXIT
$!
$ NOOBJ:
$ SAY "No object produced for ", F, " - CC reported success but wrote nothing."
$ SAY "If DCL printed TKNOVF above, the compile command exceeded 255"
$ SAY "characters and was truncated; shorten CFLAGS."
$ EXIT %X10000004
$!
$ ERROR:
$ SAY "Build failed"
$ EXIT %X10000004
