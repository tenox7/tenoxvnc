# TenoxVNC Roadmap

- Change settings from F8 menu and reconnect (LIVE-CONFIG-SWITCH)
- Optimize jpeg for performance
- Support mondern VNC server auth
- Fix jpeg fatal error handling: tight.c uses stock jpeg_std_error, whose
  error_exit calls exit(). A malformed JPEG from the server kills the viewer
  instead of dropping the rect. jpegError only catches source underruns.
- Draw the soft cursor with a clip mask (SOFT-CURSOR below)

## SOFT-CURSOR: draw it with a clip mask

SoftCursorDraw() in cursor.c puts every opaque cursor pixel with its own
XPutImage/XShmPutImage, so one cursor movement costs an X request per pixel of
the shape, around 190 for an arrow. Inherited verbatim from TightVNC 1.3.10,
FIXME comment included. Measured on Tru64 V5.1B: 794 ms of dispatch per motion
event, X server at 97% while the pointer moves, viewer itself at 0.2%.
SendPointerEvent() redraws the cursor on every local motion event, so it is
local pointer movement that triggers it, not the server.

The fix is a clip mask: build a Pixmap of the cursor and a 1-bit bitmap from
rcMask once per shape, keep a GC carrying that clip mask, and per move do
XSetClipOrigin plus XCopyArea. Two requests, no round trip, and the image is
never touched. Filling the Pixmap needs the server-to-local pixel conversion
that today only exists inside CopyDataToScreen.

Do not batch it by putting the whole cursor rectangle from the local image
instead. That was tried and it corrupts: CopyRect is handled as XCopyArea on
the window alone (rfbproto.c), so image is not a mirror of the framebuffer and
is stale wherever the remote desktop scrolled. Putting the transparent pixels
from it drags a block of stale pixels around with the cursor. It also needs an
XSync per move to keep shm safe, which cancels most of the saving.

## Further zlib and jpeg trimming

- Two perf levers need files restored from git: dct_method = JDCT_IFAST wants
  jidctfst.c in place of jidctint.c (file-count neutral), and
  do_fancy_upsampling = FALSE wants jdmerge.c for the merged upsample and
  colour-convert fast path on 4:2:0 and 4:2:2, which is what servers send.
- Another ~8K needs hand edits, not config switches: jerror.c's message table
  is 4.6K of strings plus a 1K pointer table, jdmarker.c still carries
  SOF1/2/3/5/6/7/9/10/13/14 and JFIF/Adobe APPn parsing, and jmemmgr.c keeps
  the virtual array and backing store machinery that multiscan-off makes
  unused. inflate.c has ~2.6K of unused entry points (inflateSync, Copy,
  Get/SetDictionary, GetHeader, Prime, Mark, Reset2, Undermine, SyncPoint).
  Not inflateValidate - the decoders now call it to switch the adler32 pass
  off.
- jpeg.h still carries the whole compress-side struct and the encoder
  prototypes, about half of the jpeglib.h section, all unreachable. Dropping
  them saves nothing measurable now that it is parsed once per build.
- Compile time is now X11-bound. Each of the 21 viewer TUs pulls 8369
  preprocessed lines of Xlib.h and Intrinsic.h, 69% of the TU, about 176K
  lines over the build. Only a viewer unity TU collects that, at the cost of
  the edit-compile cycle: one viewer edit would rebuild all 21.
- Re-check peak compiler memory under MIPSpro and HP-UX cc. Measured on arm64
  clang only: above a 25.2 MB empty-TU floor jpeg.c costs 42.0 MB against
  jdmarker.c's 25.8 MB, so the working set grows about 1.6x over the worst
  single module rather than with the module count.
