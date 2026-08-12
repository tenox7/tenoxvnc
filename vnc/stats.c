/*
 *  Copyright (C) 2026 TenoxVNC.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * stats.c - live diagnostics window: protocol debug, profiling, counters,
 * distributions and charts.
 *
 * Counters are bumped from the socket, protocol and decoder layers into the
 * global vncStats.  The window is a separate top-level shell holding a plain
 * core widget which we draw ourselves with Xlib (nothing beyond what the rest
 * of the viewer already uses), double buffered through a pixmap.  It has four
 * pages, everything is redrawn from scratch on a timer, so nothing here can
 * get out of sync with the counters.
 *
 * Per rectangle timing costs two gettimeofday() calls per rectangle, so it
 * only starts once the window has been opened for the first time.
 */

#include "vncviewer.h"

#ifndef VNCSTATS

/* Built without -DVNCSTATS: nothing here is compiled in.  The definition
   below only keeps this from being an empty translation unit. */

int vncStatsCompiledOut = 0;

#else

#include <X11/Xutil.h>

VncStats vncStats;
Bool statsProfiling = False;

#define PAD		8
#define WIN_W		920
#define WIN_H		740
#define WIN_MIN_W	560
#define WIN_MIN_H	400

#define SAMPLE_MS	500		/* time between samples */
#define HIST		180		/* samples kept = 90 seconds */
#define LOGSIZE		256		/* protocol log ring size */

#define PING_MAGIC	"TVNCPING"
#define PING_LEN	8
#define PING_TIMEOUT	5.0

/* pages */

enum { PG_OVERVIEW, PG_PROFILE, PG_PROTOCOL, PG_STATS, NPAGES };

static const char *pageNames[NPAGES] = {
  "OVERVIEW", "PROFILING", "PROTOCOL", "STATISTICS"
};

/* charts */

enum {
  CH_FPS, CH_RECTS, CH_NET, CH_PIXELS,
  CH_RATIO, CH_LATENCY, CH_DECODE, CH_LOAD,
  NCHARTS
};

/* colours */

enum {
  C_BG, C_PANEL, C_GRID, C_FG, C_DIM,
  C_GREEN, C_CYAN, C_ORANGE, C_VIOLET, C_YELLOW, C_RED, C_BLUE, C_PINK,
  NCOLOURS
};

static const char *colourSpecs[NCOLOURS] = {
  "#0d1117", "#161c24", "#2c3742", "#dbe4ee", "#8b98a6",
  "#4cd38a", "#4fc3f7", "#ffb74d", "#b39ddb", "#ffe082",
  "#ef5350", "#7986cb", "#f06292"
};

typedef struct {
  const char *title;
  int nseries;
  int colour[2];
  const char *legend[2];
} ChartDef;

static const ChartDef chartDefs[NCHARTS] = {
  { "Frame rate",	1, { C_GREEN,  0        }, { "updates/s", 0 } },
  { "Rectangles",	1, { C_VIOLET, 0        }, { "rects/s",   0 } },
  { "Network",		2, { C_CYAN,   C_ORANGE }, { "rx", "tx"     } },
  { "Pixel rate",	1, { C_YELLOW, 0        }, { "Mpix/s",    0 } },
  { "Compression",	1, { C_PINK,   0        }, { "ratio",     0 } },
  { "Latency",		2, { C_RED,    C_BLUE   }, { "fence", "update" } },
  { "Decode time",	2, { C_BLUE,   C_DIM    }, { "decode", "wait"  } },
  { "Decoder load",	1, { C_GREEN,  0        }, { "%",         0 } }
};

static float hist[NCHARTS][2][HIST];
static char chartValue[NCHARTS][64];
static int histHead = 0, histCount = 0;

/* histogram bucket edges and labels, shared shapes for several counters */

static const double pixEdges[STAT_NBUCKETS] = {
  256, 1024, 4096, 16384, 65536, 262144, 1048576, 0
};
static const char *pixLabels[STAT_NBUCKETS] = {
  "< 256", "< 1K", "< 4K", "< 16K", "< 64K", "< 256K", "< 1M", ">= 1M"
};

static const double rectEdges[STAT_NBUCKETS] = {
  2, 5, 9, 17, 33, 65, 129, 0
};
static const char *rectLabels[STAT_NBUCKETS] = {
  "1", "2-4", "5-8", "9-16", "17-32", "33-64", "65-128", "> 128"
};

static const double msEdges[STAT_NBUCKETS] = {
  1, 2, 5, 10, 20, 50, 100, 0
};
static const char *msLabels[STAT_NBUCKETS] = {
  "< 1", "< 2", "< 5", "< 10", "< 20", "< 50", "< 100", ">= 100"
};

static const double ivEdges[STAT_NBUCKETS] = {
  5, 10, 20, 33, 50, 100, 200, 0
};
static const char *ivLabels[STAT_NBUCKETS] = {
  "< 5", "< 10", "< 20", "< 33", "< 50", "< 100", "< 200", ">= 200"
};

/* protocol log */

typedef struct {
  double t;
  int dir;			/* 0 = from server, 1 = to server, -1 note */
  int repeat;
  double bytes;
  double aux;
  char text[40];
} LogEntry;

static LogEntry logbuf[LOGSIZE];
static int logHead = 0, logCount = 0;

/* snapshot of the counters at the previous sample, for rate calculations */

typedef struct {
  double t;
  double sockIn, sockOut, pixels, rectBytes, rawEquiv, decodeTime, waitTime;
  unsigned long updates, rects;
} Snapshot;

static Snapshot prev;

static double peakRx = 0.0, peakTx = 0.0, peakFps = 0.0;

/* window state */

static Widget statsShell = NULL, statsCanvas = NULL;
static Window statsWin = 0;
static Pixmap statsBuf = 0, stippleBm = 0;
static GC sgc = 0;
static XFontStruct *fnSmall = NULL, *fnBold = NULL;
static unsigned long colours[NCOLOURS];
static XtIntervalId statsTimer = 0;
static Bool statsUp = False, statsPaused = False;
static int bufW = 0, bufH = 0;
static int lineH, charW;
static int page = PG_OVERVIEW;
static unsigned long curFg = ~0UL;
static int tabX[NPAGES], tabW[NPAGES], tabY, tabH;

/* latency probe state */

static Bool pingActive = False;
static double pingSent = 0.0;
static double lastReqTime = 0.0;

/* per update accumulators */

static double updStart, updWait, updBytes, updPrevStart = 0.0;
static unsigned long updRects;

static void CreateStatsWindow(void);
static void StatsTimerProc(XtPointer client, XtIntervalId *id);
static void StatsEventProc(Widget w, XtPointer p, XEvent *ev, Boolean *cont);
static void Sample(void);
static void Redraw(void);


/*
 * StatsTime - seconds since the epoch as a double.
 */

double
StatsTime(void)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}


/*
 * Bucket - index of v in a set of upper bucket edges, last bucket is open.
 */

static int
Bucket(double v, const double *edges)
{
  int i;

  for (i = 0; i < STAT_NBUCKETS - 1; i++)
    if (v < edges[i])
      return i;
  return STAT_NBUCKETS - 1;
}


/*
 * StatsInit - called once the RFB connection is up.
 */

void
StatsInit(void)
{
  memset((char *)&vncStats, 0, sizeof(vncStats));
  vncStats.startTime = StatsTime();
  vncStats.latencyMin = -1.0;
  vncStats.minUpdBytes = -1.0;
  vncStats.minInterval = -1.0;
  vncStats.minRects = -1.0;
  vncStats.minDecodeMs = -1.0;

  memset((char *)&prev, 0, sizeof(prev));
  prev.t = vncStats.startTime;
  histHead = histCount = 0;
  logHead = logCount = 0;
  updPrevStart = 0.0;
  peakRx = peakTx = peakFps = 0.0;
}


/*
 * StatsLog - append one line to the protocol log.  Identical consecutive
 * entries are folded into a repeat count so that a stream of pointer events
 * does not flush everything else out of the ring.
 */

void
StatsLog(int dir, const char *text, double bytes, double aux)
{
  LogEntry *e;
  int last = (logHead - 1 + LOGSIZE) % LOGSIZE;

  if (logCount > 0 && logbuf[last].dir == dir &&
      strcmp(logbuf[last].text, text) == 0) {
    logbuf[last].repeat++;
    logbuf[last].bytes += bytes;
    logbuf[last].aux += aux;
    logbuf[last].t = StatsTime() - vncStats.startTime;
    return;
  }

  e = &logbuf[logHead];
  e->t = StatsTime() - vncStats.startTime;
  e->dir = dir;
  e->repeat = 1;
  e->bytes = bytes;
  e->aux = aux;
  strncpy(e->text, text, sizeof(e->text) - 1);
  e->text[sizeof(e->text) - 1] = 0;

  logHead = (logHead + 1) % LOGSIZE;
  if (logCount < LOGSIZE)
    logCount++;
}


/*
 * StatsRectBegin / StatsRectEnd - account for one rectangle of a framebuffer
 * update.  Wall time spent blocked on the socket inside the rectangle is
 * subtracted so that the profile shows decode cost, not network cost.
 */

void
StatsRectBegin(VncRectProfile *r)
{
  r->bytes = vncStats.streamIn;
  r->wait = vncStats.waitTime;
  r->time = statsProfiling ? StatsTime() : 0.0;
}

void
StatsRectEnd(VncRectProfile *r, CARD32 enc, int w, int h)
{
  double px = (double)w * (double)h;
  double bytes = vncStats.streamIn - r->bytes;
  double secs = 0.0;
  int idx;

  if (statsProfiling) {
    secs = (StatsTime() - r->time) - (vncStats.waitTime - r->wait);
    if (secs < 0.0)
      secs = 0.0;
  }

  switch (enc) {
  case rfbEncodingRaw:		idx = STAT_ENC_RAW;      break;
  case rfbEncodingCopyRect:	idx = STAT_ENC_COPYRECT; break;
  case rfbEncodingRRE:		idx = STAT_ENC_RRE;      break;
  case rfbEncodingCoRRE:	idx = STAT_ENC_CORRE;    break;
  case rfbEncodingHextile:	idx = STAT_ENC_HEXTILE;  break;
  case rfbEncodingZlib:		idx = STAT_ENC_ZLIB;     break;
  case rfbEncodingTight:	idx = STAT_ENC_TIGHT;    break;
  case rfbEncodingZRLE:		idx = STAT_ENC_ZRLE;     break;
  default:			idx = -1;                break;
  }

  if (idx < 0) {
    vncStats.pseudoRects++;
    vncStats.pseudoBytes += bytes;
    switch (enc) {
    case rfbEncodingXCursor:
    case rfbEncodingRichCursor:
      vncStats.cursorShapes++;
      StatsLog(0, "pseudo XCursor/RichCursor", bytes, 0.0);
      break;
    case rfbEncodingPointerPos:
      vncStats.cursorMoves++;
      break;
    case rfbEncodingLastRect:
      vncStats.lastRects++;
      break;
    case rfbEncodingNewFBSize:
    case rfbEncodingExtendedDesktopSize:
      vncStats.fbResizes++;
      StatsLog(0, "pseudo DesktopSize", bytes, 0.0);
      break;
    case rfbEncodingDesktopName:
      vncStats.nameChanges++;
      StatsLog(0, "pseudo DesktopName", bytes, 0.0);
      break;
    }
    return;
  }

  vncStats.rects++;
  vncStats.rectBytes += bytes;
  vncStats.pixels += px;
  vncStats.rawEquiv += px * (myFormat.bitsPerPixel / 8);
  vncStats.enc[idx].rects++;
  vncStats.enc[idx].bytes += bytes;
  vncStats.enc[idx].pixels += px;
  vncStats.enc[idx].time += secs;
  vncStats.histRectPix[Bucket(px, pixEdges)]++;
  updRects++;
}


/*
 * StatsUpdateStart / StatsUpdateEnd bracket the handling of one
 * FramebufferUpdate message.
 */

void
StatsUpdateStart(void)
{
  updStart = StatsTime();
  updWait = vncStats.waitTime;
  updBytes = vncStats.streamIn;
  updRects = 0;
}

void
StatsUpdateEnd(void)
{
  double now = StatsTime();
  double wall = now - updStart;
  double wait = vncStats.waitTime - updWait;
  double decode = wall - wait;
  double bytes = vncStats.streamIn - updBytes;
  double ms;

  if (decode < 0.0)
    decode = 0.0;

  vncStats.updates++;
  vncStats.decodeTime += decode;
  ms = decode * 1000.0;
  vncStats.lastDecodeMs = ms;
  vncStats.lastWaitMs = wait * 1000.0;
  if (ms > vncStats.maxDecodeMs)
    vncStats.maxDecodeMs = ms;
  if (vncStats.minDecodeMs < 0.0 || ms < vncStats.minDecodeMs)
    vncStats.minDecodeMs = ms;
  vncStats.histDecode[Bucket(ms, msEdges)]++;

  if (vncStats.minUpdBytes < 0.0 || bytes < vncStats.minUpdBytes)
    vncStats.minUpdBytes = bytes;
  if (bytes > vncStats.maxUpdBytes)
    vncStats.maxUpdBytes = bytes;
  vncStats.histUpdBytes[Bucket(bytes, pixEdges)]++;

  if (vncStats.minRects < 0.0 || updRects < vncStats.minRects)
    vncStats.minRects = updRects;
  if (updRects > vncStats.maxRects)
    vncStats.maxRects = updRects;
  vncStats.histUpdRects[Bucket((double)updRects, rectEdges)]++;

  if (updPrevStart > 0.0) {
    double iv = (updStart - updPrevStart) * 1000.0;

    if (vncStats.minInterval < 0.0 || iv < vncStats.minInterval)
      vncStats.minInterval = iv;
    if (iv > vncStats.maxInterval)
      vncStats.maxInterval = iv;
    vncStats.sumInterval += iv;
    vncStats.nInterval++;
    vncStats.histInterval[Bucket(iv, ivEdges)]++;
  }
  updPrevStart = updStart;

  StatsLog(0, "FramebufferUpdate", bytes, (double)updRects);

  if (lastReqTime > 0.0) {
    vncStats.updateLatencyMs = (now - lastReqTime) * 1000.0;
    lastReqTime = 0.0;
  }
}


/*
 * StatsUpdateRequested - note when an incremental update was asked for, so
 * that the round trip to the reply can be timed.  Only meaningful when
 * continuous updates are off.
 */

void
StatsUpdateRequested(void)
{
  if (lastReqTime == 0.0)
    lastReqTime = StatsTime();
}


/*
 * StatsPing / StatsFencePong - latency measurement using the fence extension.
 * We send a fence carrying a magic payload and the server echoes it back.
 */

static void
StatsPing(void)
{
  double now = StatsTime();

  if (!supportsFence || appData.viewOnly)
    return;

  if (pingActive) {
    if (now - pingSent < PING_TIMEOUT)
      return;			/* still waiting */
    pingActive = False;
  }

  pingSent = now;
  pingActive = True;
  SendFence(rfbFenceFlagRequest, PING_LEN, PING_MAGIC);
}

Bool
StatsFencePong(int len, char *data)
{
  double ms;

  if (!pingActive || len != PING_LEN || memcmp(data, PING_MAGIC, PING_LEN))
    return False;

  pingActive = False;
  ms = (StatsTime() - pingSent) * 1000.0;

  vncStats.latencyMs = ms;
  vncStats.latencySum += ms;
  vncStats.latencyCount++;
  if (vncStats.latencyMin < 0.0 || ms < vncStats.latencyMin)
    vncStats.latencyMin = ms;
  if (ms > vncStats.latencyMax)
    vncStats.latencyMax = ms;
  vncStats.histLatency[Bucket(ms, msEdges)]++;

  return True;
}


/*
 * Number formatting.  Old systems have no snprintf, so these all write into
 * caller supplied buffers of a known size.
 */

static char *
FmtBytes(double v, char *buf)
{
  if (v >= 1024.0 * 1024.0 * 1024.0)
    sprintf(buf, "%.2f GB", v / (1024.0 * 1024.0 * 1024.0));
  else if (v >= 1024.0 * 1024.0)
    sprintf(buf, "%.2f MB", v / (1024.0 * 1024.0));
  else if (v >= 1024.0)
    sprintf(buf, "%.1f KB", v / 1024.0);
  else
    sprintf(buf, "%.0f B", v);
  return buf;
}

static char *
FmtCount(double v, char *buf)
{
  if (v >= 1000000000.0)
    sprintf(buf, "%.2fG", v / 1000000000.0);
  else if (v >= 1000000.0)
    sprintf(buf, "%.2fM", v / 1000000.0);
  else if (v >= 10000.0)
    sprintf(buf, "%.1fk", v / 1000.0);
  else
    sprintf(buf, "%.0f", v);
  return buf;
}

static char *
FmtTime(double secs, char *buf)
{
  long s = (long)secs;

  sprintf(buf, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
  return buf;
}


/*
 * NiceMax rounds a chart maximum up to 1, 2, 2.5 or 5 times a power of ten.
 */

static double
NiceMax(double v)
{
  double scale = 1.0, n;

  if (v <= 0.0)
    return 1.0;

  while (v / scale >= 10.0)
    scale *= 10.0;
  while (v / scale < 1.0)
    scale /= 10.0;

  n = v / scale;
  if (n <= 1.0) n = 1.0;
  else if (n <= 2.0) n = 2.0;
  else if (n <= 2.5) n = 2.5;
  else if (n <= 5.0) n = 5.0;
  else n = 10.0;

  return n * scale;
}


/*
 * Sample - take one set of readings and push them into the ring buffers.
 */

static void
Sample(void)
{
  double now = StatsTime();
  double dt = now - prev.t;
  double fps, rects, rx, tx, mpix, ratio, load, decodeMs, waitMs;
  double dBytes, dRaw, dUpdates, dRects;
  char a[32], b[32];

  if (dt < 0.05)
    return;

  dUpdates = (double)(vncStats.updates - prev.updates);
  dRects = (double)(vncStats.rects - prev.rects);
  dBytes = vncStats.rectBytes - prev.rectBytes;
  dRaw = vncStats.rawEquiv - prev.rawEquiv;

  fps = dUpdates / dt;
  rects = dRects / dt;
  rx = (vncStats.sockIn - prev.sockIn) / dt;
  tx = (vncStats.sockOut - prev.sockOut) / dt;
  mpix = (vncStats.pixels - prev.pixels) / dt / 1000000.0;
  ratio = (dBytes > 0.0) ? dRaw / dBytes : 0.0;
  load = (vncStats.decodeTime - prev.decodeTime) / dt * 100.0;
  decodeMs = (dUpdates > 0.0) ?
    (vncStats.decodeTime - prev.decodeTime) * 1000.0 / dUpdates : 0.0;
  waitMs = (dUpdates > 0.0) ?
    (vncStats.waitTime - prev.waitTime) * 1000.0 / dUpdates : 0.0;

  if (load > 100.0)
    load = 100.0;

  if (fps > peakFps) peakFps = fps;
  if (rx > peakRx) peakRx = rx;
  if (tx > peakTx) peakTx = tx;

  hist[CH_FPS][0][histHead] = (float)fps;
  hist[CH_RECTS][0][histHead] = (float)rects;
  hist[CH_NET][0][histHead] = (float)(rx / 1024.0);
  hist[CH_NET][1][histHead] = (float)(tx / 1024.0);
  hist[CH_PIXELS][0][histHead] = (float)mpix;
  hist[CH_RATIO][0][histHead] = (float)ratio;
  hist[CH_LATENCY][0][histHead] = (float)vncStats.latencyMs;
  hist[CH_LATENCY][1][histHead] = (float)vncStats.updateLatencyMs;
  hist[CH_DECODE][0][histHead] = (float)decodeMs;
  hist[CH_DECODE][1][histHead] = (float)waitMs;
  hist[CH_LOAD][0][histHead] = (float)load;

  sprintf(chartValue[CH_FPS], "%.1f fps  (peak %.1f)", fps, peakFps);
  sprintf(chartValue[CH_RECTS], "%.0f/s  (%.1f per update)", rects,
	  dUpdates > 0.0 ? dRects / dUpdates : 0.0);
  sprintf(chartValue[CH_NET], "rx %s/s  tx %s/s", FmtBytes(rx, a),
	  FmtBytes(tx, b));
  sprintf(chartValue[CH_PIXELS], "%.2f Mpix/s", mpix);
  sprintf(chartValue[CH_RATIO], "%.1f:1  (%s/s saved)", ratio,
	  FmtBytes(dRaw - dBytes > 0.0 ? (dRaw - dBytes) / dt : 0.0, a));
  if (vncStats.latencyCount)
    sprintf(chartValue[CH_LATENCY], "%.1f ms  (min %.1f avg %.1f max %.1f)",
	    vncStats.latencyMs, vncStats.latencyMin,
	    vncStats.latencySum / vncStats.latencyCount, vncStats.latencyMax);
  else
    sprintf(chartValue[CH_LATENCY], "update %.1f ms  (no fence support)",
	    vncStats.updateLatencyMs);
  sprintf(chartValue[CH_DECODE], "%.2f ms decode  %.2f ms wait", decodeMs,
	  waitMs);
  sprintf(chartValue[CH_LOAD], "%.1f %%  (peak %.1f ms/update)", load,
	  vncStats.maxDecodeMs);

  histHead = (histHead + 1) % HIST;
  if (histCount < HIST)
    histCount++;

  prev.t = now;
  prev.sockIn = vncStats.sockIn;
  prev.sockOut = vncStats.sockOut;
  prev.pixels = vncStats.pixels;
  prev.rectBytes = vncStats.rectBytes;
  prev.rawEquiv = vncStats.rawEquiv;
  prev.decodeTime = vncStats.decodeTime;
  prev.waitTime = vncStats.waitTime;
  prev.updates = vncStats.updates;
  prev.rects = vncStats.rects;
}


/*
 * Drawing primitives.
 */

static void
SetFg(int colour)
{
  if (colours[colour] == curFg)
    return;
  curFg = colours[colour];
  XSetForeground(dpy, sgc, curFg);
}

static void
Fill(int x, int y, int w, int h, int colour)
{
  if (w <= 0 || h <= 0)
    return;
  SetFg(colour);
  XFillRectangle(dpy, statsBuf, sgc, x, y, w, h);
}

static void
Frame(int x, int y, int w, int h, int colour)
{
  if (w <= 1 || h <= 1)
    return;
  SetFg(colour);
  XDrawRectangle(dpy, statsBuf, sgc, x, y, w - 1, h - 1);
}

static int
Text(int x, int y, const char *s, int colour, XFontStruct *fn)
{
  int len = strlen(s);

  SetFg(colour);
  XSetFont(dpy, sgc, fn->fid);
  XDrawString(dpy, statsBuf, sgc, x, y + fn->ascent, s, len);
  return XTextWidth(fn, s, len);
}

static void
TextRight(int x, int y, const char *s, int colour, XFontStruct *fn)
{
  int len = strlen(s);

  SetFg(colour);
  XSetFont(dpy, sgc, fn->fid);
  XDrawString(dpy, statsBuf, sgc, x - XTextWidth(fn, s, len), y + fn->ascent,
	      s, len);
}

/*
 * TextClip draws text truncated to maxW pixels, so that a long desktop name
 * or encoding list cannot run into the next column.
 */

static void
TextClip(int x, int y, const char *s, int maxW, int colour, XFontStruct *fn)
{
  char buf[128];
  int n = strlen(s);

  if (maxW < charW)
    return;
  if (XTextWidth(fn, s, n) <= maxW) {
    Text(x, y, s, colour, fn);
    return;
  }

  while (n > 1 && XTextWidth(fn, s, n) > maxW)
    n--;
  if (n > (int)sizeof(buf) - 1)
    n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = 0;
  buf[n - 1] = '~';
  Text(x, y, buf, colour, fn);
}


/*
 * Panel - titled box.  Returns the y coordinate of its first content line.
 */

static int
Panel(const char *title, int x, int y, int w, int h)
{
  Fill(x, y, w, h, C_PANEL);
  Frame(x, y, w, h, C_GRID);
  Text(x + 6, y + 3, title, C_DIM, fnBold);
  return y + 4 + lineH + 2;
}

static int
PanelHeight(int rows)
{
  return 4 + lineH + 2 + rows * lineH + 5;
}


/*
 * Item lists - the label/value blocks making up the text panels.
 */

typedef struct {
  char label[26];
  char value[80];
} Item;

static Item items[64];
static int nItems;

static void
ClearItems(void)
{
  nItems = 0;
}

static void
AddItem(const char *label, const char *value)
{
  if (nItems >= (int)(sizeof(items) / sizeof(items[0])))
    return;
  strncpy(items[nItems].label, label, sizeof(items[0].label) - 1);
  items[nItems].label[sizeof(items[0].label) - 1] = 0;
  strncpy(items[nItems].value, value, sizeof(items[0].value) - 1);
  items[nItems].value[sizeof(items[0].value) - 1] = 0;
  nItems++;
}

static int
DrawItems(const char *title, int x, int y, int w, int minColW)
{
  int cols = w / minColW;
  int rows, colW, i, cx, cy, h, labW, maxLab = 0, len;

  if (cols < 1) cols = 1;
  if (cols > 4) cols = 4;
  rows = (nItems + cols - 1) / cols;
  if (rows < 1) rows = 1;
  colW = w / cols;

  /* the label column is as wide as the longest label in this list */
  for (i = 0; i < nItems; i++) {
    len = strlen(items[i].label);
    if (len > maxLab)
      maxLab = len;
  }
  labW = (maxLab + 2) * charW;
  if (labW > colW / 2)
    labW = colW / 2;

  h = PanelHeight(rows);
  y = Panel(title, x, y, w, h);

  for (i = 0; i < nItems; i++) {
    cx = x + 6 + (i / rows) * colW;
    cy = y + (i % rows) * lineH;
    Text(cx, cy, items[i].label, C_DIM, fnSmall);
    TextClip(cx + labW, cy, items[i].value, colW - labW - 10, C_FG, fnSmall);
  }

  return h;
}


/*
 * DrawChart - one time series plot with an auto scaled y axis.
 */

static void
DrawChart(int ch, int x, int y, int w, int h)
{
  const ChartDef *def = &chartDefs[ch];
  static XPoint pts[HIST + 4];
  int plotX, plotY, plotW, plotH;
  int s, i, n, step, np, px, py, first, legX;
  double top, v, vmax;
  char buf[32];

  Fill(x, y, w, h, C_PANEL);
  Frame(x, y, w, h, C_GRID);

  Text(x + 6, y + 3, def->title, C_FG, fnBold);
  TextRight(x + w - 6, y + 3, chartValue[ch], def->colour[0], fnSmall);

  plotX = x + 6;
  plotY = y + 4 + lineH + 4;
  plotW = w - 12;
  plotH = h - (plotY - y) - 6 - lineH;
  if (plotW < 8 || plotH < 8)
    return;

  /* y scale from the largest sample in the window */
  vmax = 0.0;
  for (s = 0; s < def->nseries; s++)
    for (i = 0; i < histCount; i++) {
      v = hist[ch][s][(histHead - histCount + i + HIST) % HIST];
      if (v > vmax) vmax = v;
    }
  top = NiceMax(vmax);

  SetFg(C_GRID);
  for (i = 0; i <= 4; i++) {
    py = plotY + plotH - (plotH * i) / 4;
    XDrawLine(dpy, statsBuf, sgc, plotX, py, plotX + plotW, py);
  }

  sprintf(buf, "%.4g", top);
  Text(plotX + 2, plotY - 1, buf, C_DIM, fnSmall);

  /* one point per sample, sub sampled to the pixel width, keeping peaks */
  step = 1;
  if (histCount > plotW && plotW > 0)
    step = (histCount + plotW - 1) / plotW;

  for (s = 0; s < def->nseries; s++) {
    np = 0;
    first = HIST - histCount;
    for (i = 0; i < histCount; i += step) {
      v = 0.0;
      for (n = 0; n < step && i + n < histCount; n++) {
	double sv = hist[ch][s][(histHead - histCount + i + n + HIST) % HIST];
	if (sv > v) v = sv;
      }
      px = plotX + (int)((double)plotW * (first + i) / (double)(HIST - 1));
      py = plotY + plotH - (int)(plotH * (v / top));
      if (py < plotY) py = plotY;
      if (py > plotY + plotH) py = plotY + plotH;
      pts[np].x = px;
      pts[np].y = py;
      np++;
      if (np >= HIST)
	break;
    }

    if (np < 2)
      continue;

    /* stippled area under the curve, then the line itself */
    SetFg(def->colour[s]);
    if (s == 0 && stippleBm) {
      pts[np].x = pts[np - 1].x;
      pts[np].y = plotY + plotH;
      pts[np + 1].x = pts[0].x;
      pts[np + 1].y = plotY + plotH;
      XSetFillStyle(dpy, sgc, FillStippled);
      XFillPolygon(dpy, statsBuf, sgc, pts, np + 2, Nonconvex, CoordModeOrigin);
      XSetFillStyle(dpy, sgc, FillSolid);
    }
    XDrawLines(dpy, statsBuf, sgc, pts, np, CoordModeOrigin);
  }

  legX = plotX + 2;
  for (s = 0; s < def->nseries; s++) {
    Fill(legX, plotY + plotH + 5, 7, 7, def->colour[s]);
    legX += 10;
    legX += Text(legX, plotY + plotH + 2, def->legend[s], C_DIM, fnSmall) + 10;
  }
}


/*
 * DrawBars - a labelled horizontal bar chart, used for the histograms and
 * the message type breakdown.
 */

static int
DrawBars(const char *title, const char **labels, const unsigned long *vals,
	 int n, int x, int y, int w, int h, int colour)
{
  int i, labW, valW, barX, barW, bw, rowH, top;
  unsigned long max = 0;
  double total = 0.0;
  char buf[32], cnt[32];

  for (i = 0; i < n; i++) {
    if (vals[i] > max) max = vals[i];
    total += (double)vals[i];
  }

  if (h < PanelHeight(n))
    h = PanelHeight(n);
  top = Panel(title, x, y, w, h);
  rowH = (h - (top - y) - 5) / n;
  if (rowH < lineH)
    rowH = lineH;

  labW = charW * 8;
  valW = charW * 10;
  barX = x + 6 + labW;
  barW = w - 12 - labW - valW;
  if (barW < 8)
    barW = 8;

  for (i = 0; i < n; i++) {
    int cy = top + i * rowH;
    int th = rowH - 4;

    if (th > lineH * 2) th = lineH * 2;
    TextRight(x + 6 + labW - 4, cy + (rowH - lineH) / 2, labels[i], C_DIM,
	      fnSmall);
    if (max > 0 && vals[i] > 0) {
      bw = (int)((double)barW * vals[i] / max);
      if (bw < 1) bw = 1;
      Fill(barX, cy + (rowH - th) / 2, bw, th, colour);
    }
    if (vals[i] > 0)
      sprintf(buf, "%s  %.0f%%", FmtCount((double)vals[i], cnt),
	      total > 0.0 ? 100.0 * vals[i] / total : 0.0);
    else
      strcpy(buf, "-");
    TextRight(x + w - 6, cy + (rowH - lineH) / 2, buf, vals[i] ? C_FG : C_DIM,
	      fnSmall);
  }

  return h;
}


/*
 * DrawEncodings - per encoding table with an inline share bar.
 */

static const char *encNames[STAT_ENC_COUNT] = {
  "raw", "copyrect", "rre", "corre", "hextile", "zlib", "tight", "zrle"
};

static int
DrawEncodings(int x, int y, int w)
{
  int h = PanelHeight(STAT_ENC_COUNT + 2);
  int i, cy, barX, barW, bw;
  int cRects, cBytes, cPix, cRatio;
  double share, ratio;
  char buf[64], a[32], b[32];

  y = Panel("ENCODINGS", x, y, w, h);

  cRects = x + 6 + charW * 19;
  cBytes = cRects + charW * 11;
  cPix = cBytes + charW * 11;
  cRatio = cPix + charW * 12;
  barX = cRatio + charW * 3;
  barW = w - (barX - x) - 8;
  if (barW < 20) barW = 0;

  Text(x + 6, y, "encoding", C_DIM, fnSmall);
  TextRight(cRects, y, "rects", C_DIM, fnSmall);
  TextRight(cBytes, y, "bytes", C_DIM, fnSmall);
  TextRight(cPix, y, "pixels", C_DIM, fnSmall);
  TextRight(cRatio, y, "ratio", C_DIM, fnSmall);
  if (barW)
    Text(barX, y, "share of rects", C_DIM, fnSmall);
  y += lineH;

  for (i = 0; i < STAT_ENC_COUNT; i++) {
    int col = vncStats.enc[i].rects ? C_FG : C_DIM;

    cy = y + i * lineH;
    share = vncStats.rects ? (double)vncStats.enc[i].rects / vncStats.rects
			   : 0.0;
    ratio = vncStats.enc[i].bytes > 0.0 ?
      vncStats.enc[i].pixels * (myFormat.bitsPerPixel / 8) /
      vncStats.enc[i].bytes : 0.0;

    Text(x + 6, cy, encNames[i], col, fnSmall);
    TextRight(cRects, cy, FmtCount((double)vncStats.enc[i].rects, buf), col,
	      fnSmall);
    TextRight(cBytes, cy, FmtBytes(vncStats.enc[i].bytes, buf), col, fnSmall);
    TextRight(cPix, cy, FmtCount(vncStats.enc[i].pixels, buf), col, fnSmall);
    if (ratio > 0.0) {
      sprintf(buf, "%.1f:1", ratio);
      TextRight(cRatio, cy, buf, C_GREEN, fnSmall);
    }
    if (barW && share > 0.0) {
      bw = (int)(barW * share);
      if (bw < 1) bw = 1;
      Fill(barX, cy + 2, bw, lineH - 4, C_CYAN);
    }
  }

  cy = y + STAT_ENC_COUNT * lineH;
  ratio = vncStats.rectBytes > 0.0 ? vncStats.rawEquiv / vncStats.rectBytes
				   : 0.0;
  Text(x + 6, cy, "total", C_FG, fnBold);
  TextRight(cRects, cy, FmtCount((double)vncStats.rects, buf), C_FG, fnSmall);
  TextRight(cBytes, cy, FmtBytes(vncStats.rectBytes, buf), C_FG, fnSmall);
  TextRight(cPix, cy, FmtCount(vncStats.pixels, buf), C_FG, fnSmall);
  sprintf(buf, "%.1f:1", ratio);
  TextRight(cRatio, cy, buf, C_GREEN, fnSmall);
  if (barW) {
    sprintf(buf, "%s raw equivalent, %s saved", FmtBytes(vncStats.rawEquiv, a),
	    FmtBytes(vncStats.rawEquiv - vncStats.rectBytes, b));
    Text(barX, cy, buf, C_DIM, fnSmall);
  }

  return h;
}


/*
 * DrawProfile - where the decode time goes, per encoding.
 */

static int
DrawProfile(int x, int y, int w)
{
  int h = PanelHeight(STAT_ENC_COUNT + 3);
  int i, cy, cTime, cShare, cPer, cThru, barX, barW, bw;
  double totalTime = 0.0, share;
  char buf[64];

  for (i = 0; i < STAT_ENC_COUNT; i++)
    totalTime += vncStats.enc[i].time;

  y = Panel("DECODE PROFILE", x, y, w, h);

  cTime = x + 6 + charW * 21;
  cShare = cTime + charW * 9;
  cPer = cShare + charW * 12;
  cThru = cPer + charW * 12;
  barX = cThru + charW * 3;
  barW = w - (barX - x) - 8;
  if (barW < 20) barW = 0;

  if (!statsProfiling) {
    Text(x + 6, y, "profiling starts when this window is first opened",
	 C_DIM, fnSmall);
    return h;
  }

  Text(x + 6, y, "encoding", C_DIM, fnSmall);
  TextRight(cTime, y, "decode", C_DIM, fnSmall);
  TextRight(cShare, y, "share", C_DIM, fnSmall);
  TextRight(cPer, y, "us/rect", C_DIM, fnSmall);
  TextRight(cThru, y, "Mpix/s", C_DIM, fnSmall);
  y += lineH;

  for (i = 0; i < STAT_ENC_COUNT; i++) {
    int col = vncStats.enc[i].rects ? C_FG : C_DIM;
    double t = vncStats.enc[i].time;

    cy = y + i * lineH;
    share = totalTime > 0.0 ? t / totalTime : 0.0;

    Text(x + 6, cy, encNames[i], col, fnSmall);
    sprintf(buf, "%.1f ms", t * 1000.0);
    TextRight(cTime, cy, buf, col, fnSmall);
    sprintf(buf, "%.0f%%", share * 100.0);
    TextRight(cShare, cy, vncStats.enc[i].rects ? buf : "-", col, fnSmall);
    if (vncStats.enc[i].rects) {
      sprintf(buf, "%.0f", t * 1000000.0 / vncStats.enc[i].rects);
      TextRight(cPer, cy, buf, col, fnSmall);
    }
    if (t > 0.0) {
      sprintf(buf, "%.1f", vncStats.enc[i].pixels / t / 1000000.0);
      TextRight(cThru, cy, buf, C_GREEN, fnSmall);
    }
    if (barW && share > 0.0) {
      bw = (int)(barW * share);
      if (bw < 1) bw = 1;
      Fill(barX, cy + 2, bw, lineH - 4, C_ORANGE);
    }
  }

  /* where the session wall clock went */
  cy = y + STAT_ENC_COUNT * lineH + 2;
  {
    double session = StatsTime() - vncStats.startTime;
    double dec = vncStats.decodeTime, wait = vncStats.waitTime;
    double idle = session - dec - wait;
    int fullW = w - 12;
    int wd, ww;

    if (idle < 0.0) idle = 0.0;
    if (session <= 0.0) session = 1.0;

    wd = (int)(fullW * dec / session);
    ww = (int)(fullW * wait / session);
    Fill(x + 6, cy + 2, wd, lineH - 4, C_BLUE);
    Fill(x + 6 + wd, cy + 2, ww, lineH - 4, C_GRID);
    Fill(x + 6 + wd + ww, cy + 2, fullW - wd - ww, lineH - 4, C_PANEL);
    Frame(x + 6, cy + 2, fullW, lineH - 4, C_GRID);

    sprintf(buf, "decode %.1fs (%.1f%%)   network wait %.1fs (%.1f%%)   "
	    "idle %.1fs", dec, 100.0 * dec / session, wait,
	    100.0 * wait / session, idle);
    Text(x + 6, cy + lineH, buf, C_DIM, fnSmall);
  }

  return h;
}


/*
 * DrawLog - the protocol debug log, newest line at the bottom.
 */

static int
DrawLog(int x, int y, int w, int h)
{
  int rows = (h - (4 + lineH + 2) - 5) / lineH;
  int i, first, cy, col;
  char buf[128], a[32];

  y = Panel("PROTOCOL LOG", x, y, w, h);

  if (rows < 1)
    return h;
  if (rows > logCount)
    rows = logCount;
  first = logCount - rows;

  for (i = 0; i < rows; i++) {
    LogEntry *e = &logbuf[(logHead - logCount + first + i + LOGSIZE) % LOGSIZE];

    cy = y + i * lineH;
    col = (e->dir == 1) ? C_ORANGE : (e->dir == 0 ? C_CYAN : C_VIOLET);

    sprintf(buf, "%9.3f", e->t);
    Text(x + 6, cy, buf, C_DIM, fnSmall);
    Text(x + 6 + charW * 10, cy,
	 e->dir == 1 ? "C>S" : (e->dir == 0 ? "S>C" : "---"), col, fnSmall);
    Text(x + 6 + charW * 14, cy, e->text, C_FG, fnSmall);

    if (e->repeat > 1) {
      sprintf(buf, "x%d", e->repeat);
      Text(x + 6 + charW * 46, cy, buf, C_YELLOW, fnSmall);
    }
    if (e->aux > 0.0) {
      sprintf(buf, "%.0f rects", e->aux);
      Text(x + 6 + charW * 53, cy, buf, C_DIM, fnSmall);
    }
    if (e->bytes > 0.0)
      TextRight(x + w - 6, cy, FmtBytes(e->bytes, a), C_DIM, fnSmall);
  }

  return h;
}


/*
 * The text panels.
 */

static void
BuildConnectionItems(void)
{
  char v[128], a[32];

  ClearItems();

  sprintf(v, "%.60s:%d", vncServerHost[0] ? vncServerHost : "(listen)",
	  vncServerPort);
  AddItem("Server", v);

  sprintf(v, "RFB 3.%d%s", protocolMinorVersion,
	  tightVncProtocol ? " (tight)" : "");
  AddItem("Protocol", v);

  AddItem("Desktop", (desktopName && desktopName[0]) ? desktopName : "-");

  sprintf(v, "%dx%d", si.framebufferWidth, si.framebufferHeight);
  AddItem("Framebuffer", v);

  sprintf(v, "%s%s", titleEncName[0] ? titleEncName : "-",
	  appData.enableJPEG ? " +jpeg" : "");
  AddItem("Encoding", v);

  AddItem("Requested", appData.encodingsString ? appData.encodingsString :
	  "auto (tight zrle hextile zlib corre rre)");

  sprintf(v, "zlib level %d, jpeg quality %d", appData.compressLevel,
	  appData.qualityLevel);
  AddItem("Compression", v);

  sprintf(v, "%d bpp, depth %d, %s%s", si.format.bitsPerPixel,
	  si.format.depth, si.format.bigEndian ? "big endian" : "little endian",
	  si.format.trueColour ? ", true colour" : ", colour map");
  AddItem("Server format", v);

  sprintf(v, "%d bpp, depth %d, %s", myFormat.bitsPerPixel, myFormat.depth,
	  appData.useBGR233 ? "bgr233" :
	  (myFormat.trueColour ? "true colour" : "colour map"));
  AddItem("Client format", v);

  if (myFormat.trueColour) {
    sprintf(v, "r %d<<%d g %d<<%d b %d<<%d", myFormat.redMax,
	    myFormat.redShift, myFormat.greenMax, myFormat.greenShift,
	    myFormat.blueMax, myFormat.blueShift);
    AddItem("Pixel layout", v);
  }

  sprintf(v, "depth %d, %d bpp", visdepth, visbpp);
  AddItem("X visual", v);

#ifdef MITSHM
  AddItem("MIT-SHM", appData.useShm ? "yes" : "no");
#else
  AddItem("MIT-SHM", "not compiled in");
#endif

  AddItem("Continuous upd", cuActive ? "active" :
	  (supportsCU ? "supported, off" : "not supported"));
  AddItem("Fence", supportsFence ? "supported" : "not seen");
  AddItem("SetDesktopSize", supportsSetDesktopSize ? "supported" :
	  "not supported");

  sprintf(v, "%s%s%s", appData.shareDesktop ? "shared" : "exclusive",
	  appData.viewOnly ? ", view only" : "",
	  appData.fullScreen ? ", full screen" : "");
  AddItem("Session", v);

  AddItem("Cursor", appData.useRemoteCursor ? "remote shape" : "local only");

  sprintf(v, "%s  (%s)", FmtTime(StatsTime() - vncStats.startTime, a),
	  statsPaused ? "paused" : "sampling");
  AddItem("Uptime", v);
}

static void
BuildCounterItems(void)
{
  char v[128], a[32], b[32];

  ClearItems();

  sprintf(v, "%s  (%s msgs)", FmtBytes(vncStats.sockIn, a),
	  FmtCount((double)vncStats.msgsIn, b));
  AddItem("Received", v);

  sprintf(v, "%s  (%s msgs)", FmtBytes(vncStats.sockOut, a),
	  FmtCount((double)vncStats.msgsOut, b));
  AddItem("Sent", v);

  sprintf(v, "%s rd / %s wr", FmtCount((double)vncStats.sockReads, a),
	  FmtCount((double)vncStats.sockWrites, b));
  AddItem("Socket calls", v);

  sprintf(v, "%s  (%.1f s total)", FmtCount((double)vncStats.sockWaits, a),
	  vncStats.waitTime);
  AddItem("Socket waits", v);

  sprintf(v, "%s protocol bytes", FmtBytes(vncStats.streamIn, a));
  AddItem("Stream", v);

  AddItem("FB updates", FmtCount((double)vncStats.updates, a));

  sprintf(v, "%s + %s pseudo", FmtCount((double)vncStats.rects, a),
	  FmtCount((double)vncStats.pseudoRects, b));
  AddItem("Rectangles", v);

  sprintf(v, "%s  (%s blitted)", FmtCount(vncStats.pixels, a),
	  FmtCount(vncStats.blitPixels, b));
  AddItem("Pixels", v);

  sprintf(v, "%s / %s", FmtCount((double)vncStats.cursorShapes, a),
	  FmtCount((double)vncStats.cursorMoves, b));
  AddItem("Cursor shape/pos", v);

  sprintf(v, "%lu resize / %lu rename / %lu lastrect", vncStats.fbResizes,
	  vncStats.nameChanges, vncStats.lastRects);
  AddItem("Pseudo rects", v);

  sprintf(v, "%lu fill / %lu jpeg / %lu basic / %lu raw", vncStats.tightFill,
	  vncStats.tightJpeg, vncStats.tightBasic, vncStats.tightRaw);
  AddItem("Tight rects", v);

  sprintf(v, "%lu copy / %lu palette / %lu gradient", vncStats.tightCopy,
	  vncStats.tightPalette, vncStats.tightGradient);
  AddItem("Tight filters", v);

  sprintf(v, "%s  (%lu zlib resets)", FmtBytes(vncStats.tightJpegBytes, a),
	  vncStats.tightResets);
  AddItem("JPEG data", v);

  sprintf(v, "%s in / %s out", FmtBytes(vncStats.zlibIn, a),
	  FmtBytes(vncStats.zlibOut, b));
  AddItem("Zlib stream", v);

  sprintf(v, "%s in / %s out", FmtBytes(vncStats.zrleIn, a),
	  FmtBytes(vncStats.zrleOut, b));
  AddItem("ZRLE stream", v);

  sprintf(v, "%s", FmtCount((double)vncStats.zrleTiles, a));
  AddItem("ZRLE tiles", v);

  sprintf(v, "%lu put / %lu shm", vncStats.putImages, vncStats.shmPutImages);
  AddItem("X images", v);

  sprintf(v, "%lu copyarea / %lu fillrect", vncStats.copyAreas,
	  vncStats.fillRects);
  AddItem("X draw ops", v);

  sprintf(v, "%.2f ms  (peak %.2f ms)", vncStats.lastDecodeMs,
	  vncStats.maxDecodeMs);
  AddItem("Last decode", v);

  sprintf(v, "%.1f s decode / %.1f s wait", vncStats.decodeTime,
	  vncStats.waitTime);
  AddItem("Time split", v);
}

static void
BuildMessageItems(void)
{
  char v[128];

  ClearItems();

  sprintf(v, "%lu", vncStats.msgUpdate);
  AddItem("S>C FramebufUpd", v);
  sprintf(v, "%lu", vncStats.msgColourMap);
  AddItem("S>C SetColourMap", v);
  sprintf(v, "%lu", vncStats.msgBell);
  AddItem("S>C Bell", v);
  sprintf(v, "%lu", vncStats.msgCutText);
  AddItem("S>C CutText", v);
  sprintf(v, "%lu", vncStats.msgFence);
  AddItem("S>C Fence", v);
  sprintf(v, "%lu", vncStats.msgEndCU);
  AddItem("S>C EndOfContUpd", v);

  sprintf(v, "%lu", vncStats.sentUpdateReq);
  AddItem("C>S UpdReq incr", v);
  sprintf(v, "%lu", vncStats.sentFullReq);
  AddItem("C>S UpdReq full", v);
  sprintf(v, "%lu", vncStats.sentKey);
  AddItem("C>S KeyEvent", v);
  sprintf(v, "%lu", vncStats.sentPointer);
  AddItem("C>S PointerEvent", v);
  sprintf(v, "%lu", vncStats.sentCutText);
  AddItem("C>S CutText", v);
  sprintf(v, "%lu", vncStats.sentFence);
  AddItem("C>S Fence", v);
  sprintf(v, "%lu", vncStats.sentSetDesktopSize);
  AddItem("C>S SetDesktopSize", v);
  sprintf(v, "%lu", vncStats.sentEnableCU);
  AddItem("C>S EnableContUpd", v);
}

static void
BuildStatisticItems(void)
{
  char v[128], a[32], b[32];
  double avg;

  ClearItems();

  avg = vncStats.updates ? vncStats.streamIn / vncStats.updates : 0.0;
  sprintf(v, "%s / %s / %s",
	  FmtBytes(vncStats.minUpdBytes > 0.0 ? vncStats.minUpdBytes : 0.0, a),
	  FmtBytes(avg, b), FmtBytes(vncStats.maxUpdBytes, v + 96));
  AddItem("Update min/avg/max", v);

  avg = vncStats.updates ? (double)vncStats.rects / vncStats.updates : 0.0;
  sprintf(v, "%.0f / %.1f / %.0f",
	  vncStats.minRects > 0.0 ? vncStats.minRects : 0.0, avg,
	  vncStats.maxRects);
  AddItem("Rects min/avg/max", v);

  avg = vncStats.nInterval ? vncStats.sumInterval / vncStats.nInterval : 0.0;
  sprintf(v, "%.1f / %.1f / %.1f ms",
	  vncStats.minInterval > 0.0 ? vncStats.minInterval : 0.0, avg,
	  vncStats.maxInterval);
  AddItem("Interval min/avg/max", v);

  avg = vncStats.updates ? vncStats.decodeTime * 1000.0 / vncStats.updates
			 : 0.0;
  sprintf(v, "%.2f / %.2f / %.2f ms",
	  vncStats.minDecodeMs > 0.0 ? vncStats.minDecodeMs : 0.0, avg,
	  vncStats.maxDecodeMs);
  AddItem("Decode min/avg/max", v);

  avg = vncStats.latencyCount ? vncStats.latencySum / vncStats.latencyCount
			      : 0.0;
  sprintf(v, "%.1f / %.1f / %.1f ms (%lu probes)",
	  vncStats.latencyMin > 0.0 ? vncStats.latencyMin : 0.0, avg,
	  vncStats.latencyMax, vncStats.latencyCount);
  AddItem("Latency min/avg/max", v);

  avg = vncStats.rects ? vncStats.pixels / vncStats.rects : 0.0;
  sprintf(v, "%.0f pixels", avg);
  AddItem("Mean rect area", v);

  avg = vncStats.rects ? vncStats.rectBytes / vncStats.rects : 0.0;
  sprintf(v, "%s", FmtBytes(avg, a));
  AddItem("Mean rect bytes", v);

  avg = StatsTime() - vncStats.startTime;
  sprintf(v, "%.1f fps  (%s/s)", avg > 0.0 ? vncStats.updates / avg : 0.0,
	  FmtBytes(avg > 0.0 ? vncStats.sockIn / avg : 0.0, a));
  AddItem("Session average", v);

  sprintf(v, "%s/s rx peak, %s/s tx peak", FmtBytes(peakRx, a),
	  FmtBytes(peakTx, b));
  AddItem("Peak throughput", v);

  sprintf(v, "%.1f:1 overall", vncStats.rectBytes > 0.0 ?
	  vncStats.rawEquiv / vncStats.rectBytes : 0.0);
  AddItem("Compression", v);

  sprintf(v, "%s of %s", FmtBytes(vncStats.rawEquiv - vncStats.rectBytes, a),
	  FmtBytes(vncStats.rawEquiv, b));
  AddItem("Bandwidth saved", v);

  sprintf(v, "%s per update", FmtBytes(vncStats.updates ?
	  vncStats.sockOut / vncStats.updates : 0.0, a));
  AddItem("Upstream cost", v);
}


/*
 * Pages.
 */

static void
DrawTabs(int W)
{
  int i, x = PAD;

  tabY = PAD;
  tabH = lineH + 8;

  for (i = 0; i < NPAGES; i++) {
    int tw = XTextWidth(fnBold, pageNames[i], strlen(pageNames[i])) + 24;

    tabX[i] = x;
    tabW[i] = tw;
    Fill(x, tabY, tw, tabH, i == page ? C_GRID : C_PANEL);
    Frame(x, tabY, tw, tabH, C_GRID);
    Text(x + 12, tabY + 4, pageNames[i], i == page ? C_FG : C_DIM, fnBold);
    /* an accent bar marks the current page even where colours are scarce */
    if (i == page)
      Fill(x + 1, tabY + tabH - 3, tw - 2, 2, C_CYAN);
    x += tw + 4;
  }

  if (statsPaused)
    TextRight(W - PAD, tabY + 4, "PAUSED", C_ORANGE, fnBold);
}

static void
DrawOverview(int W, int H, int top, int bottom)
{
  int y = top, chartsY, chartsH, cols, rows, cw, chh, i, cx, cy, encH;

  BuildConnectionItems();
  y += DrawItems("CONNECTION", PAD, y, W - 2 * PAD, 44 * charW) + PAD;
  chartsY = y;

  encH = PanelHeight(STAT_ENC_COUNT + 2);
  chartsH = bottom - chartsY - PAD - encH;

  if (chartsH > 60) {
    cols = (W - 2 * PAD) / (46 * charW);
    if (cols < 1) cols = 1;
    if (cols > 4) cols = 4;
    rows = (NCHARTS + cols - 1) / cols;
    cw = (W - 2 * PAD) / cols;
    chh = chartsH / rows;

    for (i = 0; i < NCHARTS; i++) {
      cx = PAD + (i % cols) * cw;
      cy = chartsY + (i / cols) * chh;
      DrawChart(i, cx, cy, cw - PAD / 2, chh - PAD / 2);
    }
  }

  DrawEncodings(PAD, bottom - encH, W - 2 * PAD);
}

static void
DrawProfilePage(int W, int H, int top, int bottom)
{
  int y = top, w = W - 2 * PAD, rest, half;

  y += DrawProfile(PAD, y, w) + PAD;
  y += DrawEncodings(PAD, y, w) + PAD;

  BuildCounterItems();
  y += DrawItems("COUNTERS", PAD, y, w, 58 * charW) + PAD;

  /* fill whatever is left with the two decode related charts */
  rest = bottom - y;
  if (rest > 70) {
    half = (W - 2 * PAD) / 2;
    DrawChart(CH_DECODE, PAD, y, half - PAD / 2, rest);
    DrawChart(CH_LOAD, PAD + half, y, half - PAD / 2, rest);
  }
}

static void
DrawProtocolPage(int W, int H, int top, int bottom)
{
  int y = top;

  BuildMessageItems();
  y += DrawItems("MESSAGE TYPES", PAD, y, W - 2 * PAD, 30 * charW) + PAD;

  DrawLog(PAD, y, W - 2 * PAD, bottom - y);
}

static void
DrawStatsPage(int W, int H, int top, int bottom)
{
  int y = top, colW, x2, h, rowH;

  BuildStatisticItems();
  y += DrawItems("DISTRIBUTION SUMMARY", PAD, y, W - 2 * PAD, 44 * charW) + PAD;

  colW = (W - 3 * PAD) / 2;
  x2 = PAD + colW + PAD;
  rowH = (bottom - y - 2 * PAD) / 3;

  h = DrawBars("RECTANGLE AREA (pixels)", pixLabels, vncStats.histRectPix,
	       STAT_NBUCKETS, PAD, y, colW, rowH, C_VIOLET);
  DrawBars("UPDATE SIZE (bytes)", pixLabels, vncStats.histUpdBytes,
	   STAT_NBUCKETS, x2, y, colW, rowH, C_CYAN);
  y += h + PAD;

  h = DrawBars("RECTS PER UPDATE", rectLabels, vncStats.histUpdRects,
	       STAT_NBUCKETS, PAD, y, colW, rowH, C_YELLOW);
  DrawBars("UPDATE INTERVAL (ms)", ivLabels, vncStats.histInterval,
	   STAT_NBUCKETS, x2, y, colW, rowH, C_GREEN);
  y += h + PAD;

  h = DrawBars("DECODE TIME (ms)", msLabels, vncStats.histDecode,
	       STAT_NBUCKETS, PAD, y, colW, rowH, C_BLUE);
  DrawBars("FENCE LATENCY (ms)", msLabels, vncStats.histLatency,
	   STAT_NBUCKETS, x2, y, colW, rowH, C_RED);
}


/*
 * Redraw - repaint everything into the back buffer and blit it.
 */

static void
Redraw(void)
{
  Dimension w, h;
  int W, H, top, bottom;
  char buf[160];

  if (!statsUp || !statsWin)
    return;

  XtVaGetValues(statsCanvas, XtNwidth, &w, XtNheight, &h, NULL);
  W = w;
  H = h;
  if (W < 16 || H < 16)
    return;

  if (!statsBuf || W != bufW || H != bufH) {
    if (statsBuf)
      XFreePixmap(dpy, statsBuf);
    statsBuf = XCreatePixmap(dpy, statsWin, W, H,
			     DefaultDepth(dpy, DefaultScreen(dpy)));
    bufW = W;
    bufH = H;
  }

  curFg = ~0UL;
  Fill(0, 0, W, H, C_BG);

  DrawTabs(W);
  top = tabY + tabH + PAD;
  bottom = H - PAD - lineH - 4;

  switch (page) {
  case PG_PROFILE:  DrawProfilePage(W, H, top, bottom);  break;
  case PG_PROTOCOL: DrawProtocolPage(W, H, top, bottom); break;
  case PG_STATS:    DrawStatsPage(W, H, top, bottom);    break;
  default:          DrawOverview(W, H, top, bottom);     break;
  }

  sprintf(buf,
	  "1-4 or Tab = page   r = reset   p = %s   q = close      "
	  "%d samples over %d s   %d log lines",
	  statsPaused ? "resume" : "pause", histCount,
	  HIST * SAMPLE_MS / 1000, logCount);
  Text(PAD, H - PAD - lineH + 2, buf, C_DIM, fnSmall);

  XCopyArea(dpy, statsBuf, statsWin, sgc, 0, 0, W, H, 0, 0);
}


/*
 * Timer: sample, probe for latency and repaint.
 */

static void
StatsTimerProc(XtPointer client, XtIntervalId *id)
{
  statsTimer = 0;

  if (!statsUp)
    return;

  if (!statsPaused) {
    Sample();
    StatsPing();
  }
  Redraw();

  statsTimer = XtAppAddTimeOut(appContext, SAMPLE_MS, StatsTimerProc, NULL);
}


static void
StatsEventProc(Widget w, XtPointer p, XEvent *ev, Boolean *cont)
{
  int i;

  if (ev->type == ButtonPress) {
    for (i = 0; i < NPAGES; i++) {
      if (ev->xbutton.y >= tabY && ev->xbutton.y < tabY + tabH &&
	  ev->xbutton.x >= tabX[i] && ev->xbutton.x < tabX[i] + tabW[i]) {
	page = i;
	break;
      }
    }
    Redraw();
    return;
  }

  if (ev->type == Expose && ev->xexpose.count > 0)
    return;

  Redraw();
}


/*
 * Colour and font setup.  Everything degrades to black and white if the
 * display or its colormap cannot give us what we ask for.
 */

static unsigned long
AllocColour(const char *spec, unsigned long fallback)
{
  static XColor cmapEntry[256];
  static int cmapSize = -1;
  XColor c;
  Colormap defCmap = DefaultColormap(dpy, DefaultScreen(dpy));
  int screen = DefaultScreen(dpy);
  int i, best = -1;
  long dr, dg, db, dist, bestDist = 0;

  if (!XParseColor(dpy, defCmap, spec, &c))
    return fallback;
  if (XAllocColor(dpy, defCmap, &c))
    return c.pixel;

  /* The colormap is full, which is the normal case on an 8 bit display
     once the desktop has taken its share.  Settle for the closest colour
     already in it rather than dropping to plain black and white. */

  if (DefaultDepth(dpy, screen) > 8)
    return fallback;

  if (cmapSize < 0) {
    cmapSize = 1 << DefaultDepth(dpy, screen);
    if (cmapSize > 256)
      cmapSize = 256;
    for (i = 0; i < cmapSize; i++)
      cmapEntry[i].pixel = i;
    XQueryColors(dpy, defCmap, cmapEntry, cmapSize);
  }

  for (i = 0; i < cmapSize; i++) {
    dr = (long)(cmapEntry[i].red >> 8) - (long)(c.red >> 8);
    dg = (long)(cmapEntry[i].green >> 8) - (long)(c.green >> 8);
    db = (long)(cmapEntry[i].blue >> 8) - (long)(c.blue >> 8);
    dist = dr * dr + dg * dg + db * db;
    if (best < 0 || dist < bestDist) {
      best = i;
      bestDist = dist;
    }
  }

  return best >= 0 ? cmapEntry[best].pixel : fallback;
}

static XFontStruct *
LoadFont(const char **names)
{
  XFontStruct *fn;
  int i;

  for (i = 0; names[i]; i++) {
    fn = XLoadQueryFont(dpy, names[i]);
    if (fn)
      return fn;
  }
  return NULL;
}


static void
CreateStatsWindow(void)
{
  static const char *smallNames[] = {
    "6x10", "-*-fixed-medium-r-normal--10-*-*-*-*-*-*-*", "5x8", "6x13",
    "fixed", NULL
  };
  static const char *boldNames[] = {
    "6x13bold", "7x13bold", "-*-fixed-bold-r-normal--13-*-*-*-*-*-*-*",
    "6x13", "6x10", "fixed", NULL
  };
  static char stippleBits[] = { 0x05, 0x0a, 0x05, 0x0a };
  unsigned long black, white;
  int screen = DefaultScreen(dpy);
  int i;

  black = BlackPixel(dpy, screen);
  white = WhitePixel(dpy, screen);

  for (i = 0; i < NCOLOURS; i++)
    colours[i] = AllocColour(colourSpecs[i],
			     (i <= C_GRID) ? black : white);

  fnSmall = LoadFont(smallNames);
  fnBold = LoadFont(boldNames);
  if (!fnSmall)
    fnSmall = fnBold;
  if (!fnBold)
    fnBold = fnSmall;
  if (!fnSmall) {
    fprintf(stderr, "Diagnostics: no usable font found\n");
    return;
  }

  lineH = fnSmall->ascent + fnSmall->descent + 2;
  charW = XTextWidth(fnSmall, "0", 1);
  if (charW < 1)
    charW = 6;

  /* Use the default visual and colormap: the desktop window may be on a
     private colormap (-owncmap) or a BGR233 palette in which our colours
     would mean nothing. */

  statsShell = XtVaCreatePopupShell("statsShell", topLevelShellWidgetClass,
				    toplevel,
				    XtNvisual, DefaultVisual(dpy, screen),
				    XtNdepth, DefaultDepth(dpy, screen),
				    XtNcolormap, DefaultColormap(dpy, screen),
				    XtNwidth, WIN_W,
				    XtNheight, WIN_H,
				    XtNminWidth, WIN_MIN_W,
				    XtNminHeight, WIN_MIN_H,
				    NULL);

  statsCanvas = XtVaCreateManagedWidget("statsCanvas", coreWidgetClass,
					statsShell,
					XtNwidth, WIN_W,
					XtNheight, WIN_H,
					XtNbackground, colours[C_BG],
					XtNborderWidth, 0,
					NULL);

  XtAddEventHandler(statsCanvas,
		    ExposureMask | StructureNotifyMask | ButtonPressMask,
		    False, StatsEventProc, NULL);

  XtRealizeWidget(statsShell);
  statsWin = XtWindow(statsCanvas);

  sgc = XCreateGC(dpy, statsWin, 0, NULL);
  stippleBm = XCreateBitmapFromData(dpy, statsWin, stippleBits, 4, 4);
  if (stippleBm)
    XSetStipple(dpy, sgc, stippleBm);
}


/*
 * ShowStats is an action which pops up the diagnostics window, creating it
 * on first use.  Per rectangle profiling starts here.
 */

void
ShowStats(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  if (!statsShell) {
    CreateStatsWindow();
    if (!statsShell)
      return;
    statsProfiling = True;
    StatsLog(-1, "profiling started", 0.0, 0.0);
  }

  if (!statsUp) {
    statsUp = True;
    XtPopup(statsShell, XtGrabNone);
    XSetWMProtocols(dpy, XtWindow(statsShell), &wmDeleteWindow, 1);
    if (!statsTimer)
      statsTimer = XtAppAddTimeOut(appContext, 50, StatsTimerProc, NULL);
  } else {
    XRaiseWindow(dpy, XtWindow(statsShell));
  }

  Redraw();
}


/*
 * HideStats is an action which pops the diagnostics window down and stops
 * sampling.  The counters keep running, they are just not displayed.
 */

void
HideStats(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  if (!statsUp)
    return;

  statsUp = False;
  if (statsTimer) {
    XtRemoveTimeOut(statsTimer);
    statsTimer = 0;
  }
  XtPopdown(statsShell);
}


/*
 * StatsPage is an action which selects a page, either by number or, with no
 * argument, the next one.
 */

void
StatsPage(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  int n;

  if (*num_params == 0) {
    page = (page + 1) % NPAGES;
  } else {
    n = atoi(params[0]) - 1;
    if (n >= 0 && n < NPAGES)
      page = n;
  }
  Redraw();
}


/*
 * ResetStats is an action which zeroes all counters and history.
 */

void
ResetStats(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  StatsInit();
  memset((char *)hist, 0, sizeof(hist));
  memset((char *)chartValue, 0, sizeof(chartValue));
  pingActive = False;
  lastReqTime = 0.0;
  StatsLog(-1, "counters reset", 0.0, 0.0);
  Redraw();
}


/*
 * PauseStats is an action which freezes sampling, leaving the last 90
 * seconds on screen for a closer look.
 */

void
PauseStats(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  statsPaused = !statsPaused;
  Redraw();
}

#endif /* VNCSTATS */
