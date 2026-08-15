# TenoxVNC

VNC viewer for vintage Unix & VMS in plain C and X11. 

Based on TightVNC, with TigerVNC feature backports (dynamic desktop resizing, continuous updates, fence, desktop name, ZRLE).

![TenoxVNC running on HP-UX](tenoxvnc.png)

## Downloads

Statically linked binaries for many OSes available under [Releases](https://github.com/tenox7/tenoxvnc/releases).


## Connection Parameters

Shown when no server is given on the command line.

![Connection dialog](connect.png)

- **Server** — `host`, `host:display` or `host::port`.
- **Password** — VNC password, sent only if the server asks.
- **Shared** *(server)* — don't kick off other viewers.
- **View only** *(client)* — never send keyboard or mouse.
- **Continuous updates** *(server)* — server pushes updates unrequested.
- **Resize remote desktop** *(server)* — window resize resizes the server framebuffer.
- **Encoding** *(server)* — Auto, Tight, ZRLE, Hextile or Raw.
- **JPEG** *(server)* — allow lossy JPEG in Tight.
- **Quality** *(server)* — JPEG quality 0-9.
- **Compress** *(server)* — zlib level 0-9, auto = 1.
- **Color** *(server)* — Full, Medium (BGR233), Low (BGR222), Very low (BGR111).
- **True color** *(client)* — force a TrueColor X visual.
- **Own colormap** *(client)* — force PseudoColor with a private colormap.
- **Depth** *(client)* — X visual depth 8/15/16/24/32, needs True color.

## Supported Platforms

- AIX 4.x; 5.x
- HP-UX 10.x; 11.x
- IRIX 5.x; 6.x
- Tru64 (DEC Unix / OSF/1) 5.x
- SCO OpenServer 5.x; 6.x
- SCO UnixWare 7.x
- SunOS 4.x; 5.x (Solaris)
- OpenVMS 7.x; 8.x

## Credits

- TightVNC (Constantin Kaplinsky, AT&T Labs Cambridge)
- TigerVNC extensions (Cendio AB)
- zlib (Gailly/Adler)
- IJG jpeg-6b.
