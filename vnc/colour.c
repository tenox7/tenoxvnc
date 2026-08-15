/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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
 * colour.c - functions to deal with colour - i.e. RFB pixel formats, X visuals
 * and colormaps.  Thanks to Grant McDorman for some of the ideas used here.
 */

#include "vncviewer.h"
#include <limits.h>


#define INVALID_PIXEL 0xffffffff
#define MAX_CMAP_SIZE 256
#define COLOUR_MAP_SIZE 256
unsigned long colourToPixel[COLOUR_MAP_SIZE];
Bool useColourMap = False;

Colormap cmap;
Visual *vis;
unsigned int visdepth, visbpp;
Bool allocColorFailed = False;

/* The reduced colour formats, indexed by COLOUR_FULL..COLOUR_VERYLOW.  All
   are 8 bits per pixel with the components packed low to high in red, green,
   blue order, which is what makes the 256-colour one BGR233. */
static const struct {
  int redMax, greenMax, blueMax;
} colourLevels[] = {
  { 0, 0, 0 },			/* COLOUR_FULL - not a reduced format */
  { 7, 7, 3 },			/* COLOUR_MEDIUM */
  { 3, 3, 3 },			/* COLOUR_LOW */
  { 1, 1, 1 }			/* COLOUR_VERYLOW */
};

static int nColoursAllocated;

static Bool GetPseudoColorVisualAndCmap(int depth);
static Bool GetTrueColorVisualAndCmap(int depth);
static int GetBPPForDepth(int depth);
static void SetReducedFormat(int level);
static void SetupColourMap();
static void AllocateExactColours();
static Bool AllocateColour(int r, int g, int b);


/*
 * Bits() is how many bits a component with this maximum value needs, and
 * ColourIndex() packs a colour into the pixel the server will send us for it.
 */

static int
Bits(int max)
{
  int n = 0;

  while (max) {
    n++;
    max >>= 1;
  }
  return n;
}

static unsigned int
ColourIndex(int r, int g, int b)
{
  return ((unsigned int)r << myFormat.redShift) |
	 ((unsigned int)g << myFormat.greenShift) |
	 ((unsigned int)b << myFormat.blueShift);
}


/*
 * SetVisualAndCmap() deals with the wonderful world of X "visuals" (which are
 * equivalent to the RFB protocol's "pixel format").  Having decided on the
 * best visual, it also creates a colormap if necessary, sets the appropriate
 * resources on the toplevel widget, and sets up the myFormat structure to
 * describe the pixel format in terms that the RFB server will be able to
 * understand.
 *
 * The algorithm for deciding which visual to use is as follows:
 *
 * If forceOwnCmap is true then we try to use a PseudoColor visual - we first
 * see if there's one of the same depth as the RFB server, followed by an 8-bit
 * deep one.
 *
 * If forceTrueColour is true then we try to use a TrueColor visual - if
 * requestedDepth is set then it must be of that depth, otherwise any depth
 * will be used.
 *
 * Otherwise, we use the X server's default visual and colormap.  If this is
 * TrueColor and full colour was asked for then we just ask the RFB server for
 * this format.  If the default isn't TrueColor, or if a reduced colour level
 * was asked for, then we ask the RFB server for an 8-bit format of that many
 * colours and use a lookup table to translate to the nearest colours provided
 * by the X server.
 */

void
SetVisualAndCmap()
{
  if (appData.forceOwnCmap) {
    if (!si.format.trueColour) {
      if (GetPseudoColorVisualAndCmap(si.format.depth))
	return;
    }
    if (GetPseudoColorVisualAndCmap(8))
      return;
    fprintf(stderr,"Couldn't find a matching PseudoColor visual.\n");
  }

  if (appData.forceTrueColour) {
    if (GetTrueColorVisualAndCmap(appData.requestedDepth))
      return;
    fprintf(stderr,"Couldn't find a matching TrueColor visual.\n");
  }

  /* just use default visual and colormap */

  vis = DefaultVisual(dpy,DefaultScreen(dpy));
  visdepth = DefaultDepth(dpy,DefaultScreen(dpy));
  visbpp = GetBPPForDepth(visdepth);
  cmap = DefaultColormap(dpy,DefaultScreen(dpy));

  if (appData.colourLevel == COLOUR_FULL && vis->class == TrueColor) {

    myFormat.bitsPerPixel = visbpp;
    myFormat.depth = visdepth;
    myFormat.trueColour = 1;
    myFormat.bigEndian = (ImageByteOrder(dpy) == MSBFirst);
    myFormat.redShift = ffs(vis->red_mask) - 1;
    myFormat.greenShift = ffs(vis->green_mask) - 1;
    myFormat.blueShift = ffs(vis->blue_mask) - 1;
    myFormat.redMax = vis->red_mask >> myFormat.redShift;
    myFormat.greenMax = vis->green_mask >> myFormat.greenShift;
    myFormat.blueMax = vis->blue_mask >> myFormat.blueShift;

    fprintf(stderr,
	    "Using default colormap which is TrueColor.  Pixel format:\n");
    PrintPixelFormat(&myFormat);
    return;
  }

  /* A default visual that isn't TrueColor cannot show full colour at all, so
     fall back to the 256-colour format the way the viewer always has. */
  if (appData.colourLevel == COLOUR_FULL)
    appData.colourLevel = COLOUR_MEDIUM;

  SetReducedFormat(appData.colourLevel);

  fprintf(stderr,
	  "Using default colormap and translating from %s.  Pixel format:\n",
	  ColourModeName());
  PrintPixelFormat(&myFormat);

  SetupColourMap();
}


/*
 * SetReducedFormat asks the server for one of the 8bpp formats in
 * colourLevels[], which we then have to translate through colourToPixel[].
 */

static void
SetReducedFormat(int level)
{
  useColourMap = True;

  myFormat.bitsPerPixel = 8;
  myFormat.trueColour = 1;
  myFormat.bigEndian = 0;
  myFormat.redMax = colourLevels[level].redMax;
  myFormat.greenMax = colourLevels[level].greenMax;
  myFormat.blueMax = colourLevels[level].blueMax;
  myFormat.redShift = 0;
  myFormat.greenShift = Bits(myFormat.redMax);
  myFormat.blueShift = myFormat.greenShift + Bits(myFormat.greenMax);
  myFormat.depth = myFormat.blueShift + Bits(myFormat.blueMax);
}


/*
 * ColourModeName is how the colours are being got, for the window title and
 * the diagnostics.  Reduced formats are named the way BGR233 always has been,
 * counting the bits from the top of the pixel down.
 */

const char *
ColourModeName(void)
{
  static char name[16];

  if (!useColourMap)
    sprintf(name, "%dbit", visdepth);
  else
    sprintf(name, "bgr%d%d%d", Bits(myFormat.blueMax),
	    Bits(myFormat.greenMax), Bits(myFormat.redMax));

  return name;
}


/*
 * GetPseudoColorVisualAndCmap tries to find a PseudoColor visual of the given
 * depth.  If successful it sets vis, visdepth, cmap and myFormat, and also
 * sets the appropriate resources on the toplevel widget.
 */

static Bool
GetPseudoColorVisualAndCmap(int depth)
{
  XVisualInfo tmpl;
  XVisualInfo *vinfo;
  int nvis;

  tmpl.screen = DefaultScreen(dpy);
  tmpl.depth = depth;
  tmpl.class = PseudoColor;
  tmpl.colormap_size = (1 << depth);

  vinfo = XGetVisualInfo(dpy,
			 VisualScreenMask|VisualDepthMask|
			 VisualClassMask|VisualColormapSizeMask,
			 &tmpl, &nvis);

  if (vinfo) {
    vis = vinfo[0].visual;
    visdepth = vinfo[0].depth;
    XFree(vinfo);
    visbpp = GetBPPForDepth(visdepth);
    myFormat.bitsPerPixel = visbpp;
    myFormat.depth = visdepth;
    myFormat.trueColour = 0;
    myFormat.bigEndian = (ImageByteOrder(dpy) == MSBFirst);
    myFormat.redMax = myFormat.greenMax = myFormat.blueMax = 0;
    myFormat.redShift = myFormat.greenShift = myFormat.blueShift = 0;

    cmap = XCreateColormap(dpy, DefaultRootWindow(dpy), vis, AllocAll);

    XtVaSetValues(toplevel, XtNcolormap, cmap, XtNdepth, visdepth,
		  XtNvisual, vis, NULL);

    if (appData.fullScreen) {
      XInstallColormap(dpy, cmap);
    }

    fprintf(stderr,"Using PseudoColor visual, depth %d.  Pixel format:\n",
	    visdepth);
    PrintPixelFormat(&myFormat);

    return True;
  }

  return False;
}


/*
 * GetTrueColorVisualAndCmap tries to find a TrueColor visual of the given
 * depth.  If successful it sets vis, visdepth, cmap and myFormat, and also
 * sets the appropriate resources on the toplevel widget.
 */

static Bool
GetTrueColorVisualAndCmap(int depth)
{
  XVisualInfo tmpl;
  XVisualInfo *vinfo;
  int nvis;
  int mask = VisualScreenMask|VisualClassMask;

  tmpl.screen = DefaultScreen(dpy);
  tmpl.class = TrueColor;

  if (depth != 0) {
    tmpl.depth = depth;
    mask |= VisualDepthMask;
  }

  vinfo = XGetVisualInfo(dpy, mask, &tmpl, &nvis);

  if (vinfo) {
    vis = vinfo[0].visual;
    visdepth = vinfo[0].depth;
    XFree(vinfo);
    visbpp = GetBPPForDepth(visdepth);
    myFormat.bitsPerPixel = visbpp;
    myFormat.depth = visdepth;
    myFormat.trueColour = 1;
    myFormat.bigEndian = (ImageByteOrder(dpy) == MSBFirst);
    myFormat.redShift = ffs(vis->red_mask) - 1;
    myFormat.greenShift = ffs(vis->green_mask) - 1;
    myFormat.blueShift = ffs(vis->blue_mask) - 1;
    myFormat.redMax = vis->red_mask >> myFormat.redShift;
    myFormat.greenMax = vis->green_mask >> myFormat.greenShift;
    myFormat.blueMax = vis->blue_mask >> myFormat.blueShift;

    cmap = XCreateColormap(dpy, DefaultRootWindow(dpy), vis, AllocNone);

    XtVaSetValues(toplevel, XtNcolormap, cmap, XtNdepth, visdepth,
		  XtNvisual, vis, NULL);

    if (appData.fullScreen) {
      XInstallColormap(dpy, cmap);
    }

    fprintf(stderr,"Using TrueColor visual, depth %d.  Pixel format:\n",
	    visdepth);
    PrintPixelFormat(&myFormat);

    return True;
  }

  return False;
}


/*
 * GetBPPForDepth looks through the "pixmap formats" to find the bits-per-pixel
 * for the given depth.
 */

static int
GetBPPForDepth(int depth)
{
  XPixmapFormatValues *format;
  int nformats;
  int i;
  int bpp;

  format = XListPixmapFormats(dpy, &nformats);

  for (i = 0; i < nformats; i++) {
    if (format[i].depth == depth)
      break;
  }

  if (i == nformats) {
    fprintf(stderr,"no pixmap format for depth %d???\n", depth);
    exit(1);
  }

  bpp = format[i].bits_per_pixel;

  XFree(format);

  if (bpp != 1 && bpp != 8 && bpp != 16 && bpp != 32) {
    fprintf(stderr,"Can't cope with %d bits-per-pixel.  Sorry.\n", bpp);
    exit(1);
  }

  return bpp;
}




/*
 * SetupColourMap() sets up the colourToPixel array.
 *
 * It calls AllocateExactColours to allocate some exact colours from the cube
 * the server is going to send us (limited by space in the colormap and/or by
 * the value of the nColours resource).  If the number allocated is less than
 * the whole cube then it fills the rest in using the "nearest" colours
 * available.  How this is done depends on the value of the useSharedColours
 * resource.  If it's false, we use only colours from the exact colours we've
 * just allocated.  If it's true, then we also use other clients' "shared"
 * colours available in the colormap.
 */

static void
SetupColourMap()
{
  int r, g, b;
  long i;
  unsigned long nearestPixel = 0;
  int cmapSize;
  int cubeSize = 1 << myFormat.depth;
  XColor cmapEntry[MAX_CMAP_SIZE];
  Bool exact[MAX_CMAP_SIZE];
  Bool shared[MAX_CMAP_SIZE];
  Bool usedAsNearest[MAX_CMAP_SIZE];
  int nSharedUsed = 0;

  if (visdepth > 8) {
    appData.nColours = cubeSize; /* ignore nColours setting for > 8-bit deep */
  }

  for (i = 0; i < COLOUR_MAP_SIZE; i++) {
    colourToPixel[i] = INVALID_PIXEL;
  }

  AllocateExactColours();

  fprintf(stderr,"Got %d exact %s colours out of %d\n",
	  nColoursAllocated, ColourModeName(), appData.nColours);

  if (nColoursAllocated < cubeSize) {

    if (visdepth > 8) { /* shouldn't get here */
      fprintf(stderr,"Error: couldn't allocate %s colours even though "
	      "depth is %d\n", ColourModeName(), visdepth);
      exit(1);
    }

    cmapSize = (1 << visdepth);

    for (i = 0; i < cmapSize; i++) {
      cmapEntry[i].pixel = i;
      exact[i] = False;
      shared[i] = False;
      usedAsNearest[i] = False;
    }

    XQueryColors(dpy, cmap, cmapEntry, cmapSize);

    /* mark all the pixels we got exactly */

    for (i = 0; i < cubeSize; i++) {
      if (colourToPixel[i] != INVALID_PIXEL)
	exact[colourToPixel[i]] = True;
    }

    if (appData.useSharedColours) {

      /* Try to find existing shared colours.  This is harder than it sounds
	 because XQueryColors doesn't tell us whether colours are shared,
	 private or unallocated.  What we do is go through the colormap and for
	 each pixel try to allocate exactly its RGB values.  If this returns a
	 different pixel then it's definitely either a private or unallocated
	 pixel, so no use to us.  If it returns us the same pixel again, then
	 it's likely that it's a shared colour - however, it is possible that
	 it was actually an unallocated pixel, which we've now allocated.  We
	 minimise this possibility by going through the pixels in reverse order
	 - this helps becuse the X server allocates new pixels from the lowest
	 number up, so it should only be a problem for the lowest unallocated
	 pixel.  Got that? */

      for (i = cmapSize-1; i >= 0; i--) {
	if (!exact[i] &&
	    XAllocColor(dpy, cmap, &cmapEntry[i])) {

	  if (cmapEntry[i].pixel == i) {

	    shared[i] = True; /* probably shared */

	  } else {

	    /* "i" is either unallocated or private.  We have now unnecessarily
	       allocated cmapEntry[i].pixel.  Free it. */

	    XFreeColors(dpy, cmap, &cmapEntry[i].pixel, 1, 0);
	  }
	}
      }
    }

    /* Now fill in the nearest colours */

    for (r = 0; r <= myFormat.redMax; r++) {
      for (g = 0; g <= myFormat.greenMax; g++) {
	for (b = 0; b <= myFormat.blueMax; b++) {
	  if (colourToPixel[ColourIndex(r,g,b)] == INVALID_PIXEL) {

	    unsigned long minDistance = ULONG_MAX;

	    for (i = 0; i < cmapSize; i++) {
	      if (exact[i] || shared[i]) {
		unsigned long distance
		  = (abs(cmapEntry[i].red - r * 65535 / myFormat.redMax)
		     + abs(cmapEntry[i].green - g * 65535 / myFormat.greenMax)
		     + abs(cmapEntry[i].blue - b * 65535 / myFormat.blueMax));

		if (distance < minDistance) {
		  minDistance = distance;
		  nearestPixel = i;
		}
	      }
	    }

	    colourToPixel[ColourIndex(r,g,b)] = nearestPixel;
	    if (shared[nearestPixel] && !usedAsNearest[nearestPixel])
	      nSharedUsed++;
	    usedAsNearest[nearestPixel] = True;
	  }
	}
      }
    }

    /* Tidy up shared colours which we allocated but aren't going to use */

    for (i = 0; i < cmapSize; i++) {
      if (shared[i] && !usedAsNearest[i]) {
	  XFreeColors(dpy, cmap, (unsigned long *)&i, 1, 0);
      }
    }

    fprintf(stderr,"Using %d existing shared colours\n", nSharedUsed);
  }
}


/*
 * AllocateExactColours() attempts to allocate each of the colours in the
 * colour cube, stopping when an allocation fails.  The order it does this in
 * is such that we should get a fairly well spread subset of the cube, however
 * many allocations are made.  There's probably a neater algorithm for doing
 * this, but it's not obvious to me anyway.  The way this algorithm works is:
 *
 * At each stage, we introduce a new value for one of the primaries, and
 * allocate all the colours with the new value of that primary and all previous
 * values of the other two primaries.  We start with r=0 as the "new" value
 * for r, and g=0, b=0 as the "previous" values of g and b.  So for BGR233 we
 * get:
 *
 * New primary value   Previous values of other primaries   Colours allocated
 * -----------------   ----------------------------------   -----------------
 * r=0                 g=0       b=0                        r0 g0 b0
 * g=7                 r=0       b=0                        r0 g7 b0
 * b=3                 r=0       g=0,7                      r0 g0 b3
 *                                                          r0 g7 b3
 * r=7                 g=0,7     b=0,3                      r7 g0 b0
 * 		       		 			    r7 g0 b3
 * 							    r7 g7 b0
 *							    r7 g7 b3
 * g=3                 r=0,7     b=0,3                      r0 g3 b0
 *                                                          r0 g3 b3
 *                                                          r7 g3 b0
 *                                                          r7 g3 b3
 * ....etc.
 *
 * The value orders come from SpreadOrder(): the ends of the range first and
 * then its bisections, so that whichever value a primary stops at, the ones
 * taken so far are spread over the whole range rather than bunched at one end.
 * */

static const int *
SpreadOrder(int max)
{
  static const int order8[] = {0,7,3,5,1,6,2,4};
  static const int order4[] = {0,3,1,2};
  static const int order2[] = {0,1};

  if (max == 7)
    return order8;
  if (max == 3)
    return order4;
  return order2;
}

static void
AllocateExactColours()
{
  const int *rv = SpreadOrder(myFormat.redMax);
  const int *gv = SpreadOrder(myFormat.greenMax);
  const int *bv = SpreadOrder(myFormat.blueMax);
  int nr = myFormat.redMax + 1;
  int ng = myFormat.greenMax + 1;
  int nb = myFormat.blueMax + 1;
  int rn = 0;
  int gn = 1;
  int bn = 1;
  int ri, gi, bi;

  nColoursAllocated = 0;

  while (rn < nr || gn < ng || bn < nb) {

    if (rn < nr) {
      for (gi = 0; gi < gn; gi++) {
	for (bi = 0; bi < bn; bi++) {
	  if (!AllocateColour(rv[rn], gv[gi], bv[bi]))
	    return;
	}
      }
      rn++;
    }

    if (gn < ng) {
      for (ri = 0; ri < rn; ri++) {
	for (bi = 0; bi < bn; bi++) {
	  if (!AllocateColour(rv[ri], gv[gn], bv[bi]))
	    return;
	}
      }
      gn++;
    }

    if (bn < nb) {
      for (ri = 0; ri < rn; ri++) {
	for (gi = 0; gi < gn; gi++) {
	  if (!AllocateColour(rv[ri], gv[gi], bv[bn]))
	    return;
	}
      }
      bn++;
    }
  }
}


/*
 * AllocateColour() attempts to allocate the given colour as a shared colormap
 * entry, storing its pixel value in the colourToPixel array.  r, g and b run
 * from 0 to the maximum for that primary in the format we asked the server
 * for.  It fails either when the allocation fails or when we would exceed the
 * number of colours specified in the nColours resource.
 */

static Bool
AllocateColour(int r, int g, int b)
{
  XColor c;

  if (nColoursAllocated >= appData.nColours)
    return False;

  c.red = r * 65535 / myFormat.redMax;
  c.green = g * 65535 / myFormat.greenMax;
  c.blue = b * 65535 / myFormat.blueMax;

  if (!XAllocColor(dpy, cmap, &c))
    return False;

  colourToPixel[ColourIndex(r,g,b)] = c.pixel;

  nColoursAllocated++;

  return True;
}
