# TenoxVNC

Ultra portable VNC viewer for vintage Unix / X11 in plain C. 

Based on TightVNC, with TigerVNC feature backports (dynamic desktop resizing, continuous updates, fence, desktop name, ZRLE).

## Downloads

Statically linked binaries for many OSes available under [Releases](https://github.com/tenox7/tenoxvnc/releases).

## Build

    make <target>

where target is one of `linux solaris hpux hpux9 aix unixware osf1 irix irix5 netbsd macos`.

## Diagnostics

Optional live diagnostics window in the F8 menu: protocol log, decode profiling,
counters, histograms and charts. Costs a few cycles in the hot paths, so it is not
compiled in by default:

    VNCSTATS=true make hpux

## Credits

- TightVNC (Constantin Kaplinsky, AT&T Labs Cambridge)
- TigerVNC extensions (Cendio AB)
- zlib (Gailly/Adler)
- IJG jpeg-6b.

