# TenoxVNC Roadmap

- Change settings from F8 menu and reconnect (LIVE-CONFIG-SWITCH)
- Optimize jpeg for performance
- Support mondern VNC server auth
- Fix jpeg fatal error handling: tight.c uses stock jpeg_std_error, whose
  error_exit calls exit(). A malformed JPEG from the server kills the viewer
  instead of dropping the rect. jpegError only catches source underruns.

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

## Performance, non-JPEG

What is left after the pass that moved the decoders into the local image.

- ZRLE and Tight still decode into a scratch buffer and then copy that into
  the image (zrle.c CopyDataToImage per tile, tight.c per band). Raw no
  longer does. Writing at the image's stride would save a pass over every
  pixel, but only for a full-color session: the default reduced format has
  to go through colorToPixel[] anyway, so the scratch buffer earns its keep
  there. In ZRLE it also means the RLE runs have to wrap at the tile edge,
  which is where ZrleFill gets its speed.

- rfbproto.o has no dependency on the files rfbproto.c #includes, so editing
  hextile.c, rre.c, corre.c, tight.c or zrle.c does not rebuild it. Every
  measurement or test after such an edit silently runs the old code unless
  the tree is built from clean. A POSIX-portable fix is to list them as
  prerequisites of rfbproto.o.
