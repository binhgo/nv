/*
	Netvideo version 3.3
	Written by Ron Frederick <frederick@parc.xerox.com>

	X11 screen grab routines
*/

/*
 * Copyright (c) Xerox Corporation 1992. All rights reserved.
 *  
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Xerox is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Xerox grants no other licenses expressed or implied. The Xerox trade
 * name should not be used in any advertising without its written permission.
 *  
 * XEROX CORPORATION MAKES NO REPRESENTATIONS CONCERNING EITHER THE
 * MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *  
 * These notices must be retained in any copies of any part of this software.
 */

#ifdef X11GRAB
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifdef AIX
#include <net/nh.h>
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <tk.h>
#include "sized_types.h"
#include "vid_util.h"
#include "vid_image.h"
#include "vid_code.h"
#include "x11_grab.h"

#define X11GRAB_FIXED	0
#define X11GRAB_POINTER	1
#define X11GRAB_WINDOW	2

extern Tcl_Interp *interp;
extern Tk_Window tkMainWin;

static Display *dpy;
static int screen, xerror, xmit_size, xmit_color;
static Window root=None, vRoot=None, target=None;
static Colormap colormap;
static Visual *root_vis;
static XVisualInfo root_visinfo;
static int root_depth, root_width, root_height;
static int (*grab)(uint8 *)=NULL;
static uint8 *grabdata=NULL;
static reconfigproc_t *reconfig=NULL;
static void *enc_state=NULL;

static int mode, x_origin=0, y_origin=0, width=320, height=240;
static int ncolors, black, white;
static ximage_t *ximage=NULL;
static XColor *col=NULL;

/*ARGSUSED*/
static int ErrHandler(ClientData clientData, XErrorEvent *errevp)
{
    xerror = 1;
    return 0;
}

static Window VirtualRootWindow(Display *dpy, int screen)
{
    static Display *last_dpy=(Display *)NULL;
    static int last_screen = -1;
    static Window vRoot=None;

    Atom __SWM_VROOT=None;
    int i;
    Window rw, p, *child;
    unsigned int nChildren;

    if ((dpy != last_dpy) || (screen != last_screen)) {
	vRoot = RootWindow(dpy, screen);

	/* go look for a virtual root */
	__SWM_VROOT = XInternAtom(dpy, "__SWM_VROOT", False);
	XQueryTree(dpy, vRoot, &rw, &p, &child, &nChildren);
	for (i=0; i<nChildren; i++) {
	    Atom actual_type;
	    int actual_format;
	    unsigned long nitems, bytesafter;
	    Window *newRoot=NULL;

	    if ((XGetWindowProperty(dpy, child[i], __SWM_VROOT, 0, 1, False,
				    XA_WINDOW, &actual_type, &actual_format,
				    &nitems, &bytesafter,
				    (unsigned char **)&newRoot) == Success)
		&& (newRoot != NULL)) {
		vRoot = *newRoot;
		XFree((void *)newRoot);
		break;
	    }
	}
	XFree((void *)child);

	last_dpy = dpy;
	last_screen = screen;
    }

    return vRoot;
}

static void X11Grab_ComputeYUVTable(void)
{
    int i;

    switch (root_visinfo.class) {
    case StaticColor:
    case PseudoColor:
    case StaticGray:
    case GrayScale:
	for (i=0; i<ncolors; i++) col[i].pixel = i;
	XQueryColors(dpy, colormap, col, ncolors);
	for (i=0; i<ncolors; i++) {
	    col[i].red = (col[i].red >> 1) & 0x7c00;
	    col[i].green = (col[i].green >> 6) & 0x3c0;
	    col[i].blue = col[i].blue >> 11;
	}
	break;
    case TrueColor:
    case DirectColor:
	break;
    }
}

static int X11Grab_GreyWhite1MSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0] = 0xff * ((row & 0x80)>>7);
	    yp[1] = 0xff * ((row & 0x40)>>6);
	    yp[2] = 0xff * ((row & 0x20)>>5);
	    yp[3] = 0xff * ((row & 0x10)>>4);
	    yp[4] = 0xff * ((row & 0x08)>>3);
	    yp[5] = 0xff * ((row & 0x04)>>2);
	    yp[6] = 0xff * ((row & 0x02)>>1);
	    yp[7] = 0xff *  (row & 0x01);
	    yp += 8;
	}
    }

    return 1;
}

static int X11Grab_GreyBlack1MSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0] = 0xff - 0xff * ((row & 0x80)>>7);
	    yp[1] = 0xff - 0xff * ((row & 0x40)>>6);
	    yp[2] = 0xff - 0xff * ((row & 0x20)>>5);
	    yp[3] = 0xff - 0xff * ((row & 0x10)>>4);
	    yp[4] = 0xff - 0xff * ((row & 0x08)>>3);
	    yp[5] = 0xff - 0xff * ((row & 0x04)>>2);
	    yp[6] = 0xff - 0xff * ((row & 0x02)>>1);
	    yp[7] = 0xff - 0xff *  (row & 0x01);
	    yp += 8;
	}
    }

    return 1;
}

static int X11Grab_GreyWhite1LSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0] = 0xff *  (row & 0x01);
	    yp[1] = 0xff * ((row & 0x02)>>1);
	    yp[2] = 0xff * ((row & 0x04)>>2);
	    yp[3] = 0xff * ((row & 0x08)>>3);
	    yp[4] = 0xff * ((row & 0x10)>>4);
	    yp[5] = 0xff * ((row & 0x20)>>5);
	    yp[6] = 0xff * ((row & 0x40)>>6);
	    yp[7] = 0xff * ((row & 0x80)>>7);
	    yp += 8;
	}
    }

    return 1;
}

static int X11Grab_GreyBlack1LSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0] = 0xff - 0xff *  (row & 0x01);
	    yp[1] = 0xff - 0xff * ((row & 0x02)>>1);
	    yp[2] = 0xff - 0xff * ((row & 0x04)>>2);
	    yp[3] = 0xff - 0xff * ((row & 0x08)>>3);
	    yp[4] = 0xff - 0xff * ((row & 0x10)>>4);
	    yp[5] = 0xff - 0xff * ((row & 0x20)>>5);
	    yp[6] = 0xff - 0xff * ((row & 0x40)>>6);
	    yp[7] = 0xff - 0xff * ((row & 0x80)>>7);
	    yp += 8;
	}
    }

    return 1;
}

static int X11Grab_YUYVWhite1MSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0]  = 0xff * ((row & 0x80)>>7);
	    yp[2]  = 0xff * ((row & 0x40)>>6);
	    yp[4]  = 0xff * ((row & 0x20)>>5);
	    yp[6]  = 0xff * ((row & 0x10)>>4);
	    yp[8]  = 0xff * ((row & 0x08)>>3);
	    yp[10] = 0xff * ((row & 0x04)>>2);
	    yp[12] = 0xff * ((row & 0x02)>>1);
	    yp[14] = 0xff *  (row & 0x01);
	    yp[1] = yp[3] = yp[5] = yp[7] =
		yp[9] = yp[11] = yp[13] = yp[15] = 0x80;
	    yp += 16;
	}
    }

    return 1;
}

static int X11Grab_YUYVBlack1MSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0]  = 0xff - 0xff * ((row & 0x80)>>7);
	    yp[2]  = 0xff - 0xff * ((row & 0x40)>>6);
	    yp[4]  = 0xff - 0xff * ((row & 0x20)>>5);
	    yp[6]  = 0xff - 0xff * ((row & 0x10)>>4);
	    yp[8]  = 0xff - 0xff * ((row & 0x08)>>3);
	    yp[10] = 0xff - 0xff * ((row & 0x04)>>2);
	    yp[12] = 0xff - 0xff * ((row & 0x02)>>1);
	    yp[14] = 0xff - 0xff *  (row & 0x01);
	    yp[1] = yp[3] = yp[5] = yp[7] =
		yp[9] = yp[11] = yp[13] = yp[15] = 0x80;
	    yp += 16;
	}
    }

    return 1;
}

static int X11Grab_YUYVWhite1LSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0]  = 0xff *  (row & 0x01);
	    yp[2]  = 0xff * ((row & 0x02)>>1);
	    yp[4]  = 0xff * ((row & 0x04)>>2);
	    yp[6]  = 0xff * ((row & 0x08)>>3);
	    yp[8]  = 0xff * ((row & 0x10)>>4);
	    yp[10] = 0xff * ((row & 0x20)>>5);
	    yp[12] = 0xff * ((row & 0x40)>>6);
	    yp[14] = 0xff * ((row & 0x80)>>7);
	    yp[1] = yp[3] = yp[5] = yp[7] =
		yp[9] = yp[11] = yp[13] = yp[15] = 0x80;
	    yp += 16;
	}
    }

    return 1;
}

static int X11Grab_YUYVBlack1LSB(uint8 *grabdata)
{
    int x, y, row;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=8) {
	    row = *data++;

	    yp[0]  = 0xff - 0xff *  (row & 0x01);
	    yp[2]  = 0xff - 0xff * ((row & 0x02)>>1);
	    yp[4]  = 0xff - 0xff * ((row & 0x04)>>2);
	    yp[6]  = 0xff - 0xff * ((row & 0x08)>>3);
	    yp[8]  = 0xff - 0xff * ((row & 0x10)>>4);
	    yp[10] = 0xff - 0xff * ((row & 0x20)>>5);
	    yp[12] = 0xff - 0xff * ((row & 0x40)>>6);
	    yp[14] = 0xff - 0xff * ((row & 0x80)>>7);
	    yp[1] = yp[3] = yp[5] = yp[7] =
		yp[9] = yp[11] = yp[13] = yp[15] = 0x80;
	    yp += 16;
	}
    }

    return 1;
}

static int X11Grab_GreyPseudo8(uint8 *grabdata)
{
    int x, y, rgb0, rgb1;
    XColor *c0, *c1;
    uint8 *data=(uint8 *)ximage->image->data, *yp=grabdata, *ry=rgb2y;

    X11Grab_ComputeYUVTable();

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    c0 = &col[data[0]];
	    c1 = &col[data[1]];
	    data += 2;

	    rgb0 = c0->red+c0->green+c0->blue;
	    rgb1 = c1->red+c1->green+c1->blue;
	    yp[0] = ry[rgb0];
	    yp[1] = ry[rgb1];
	    yp += 2;
	}
    }

    return 1;
}

static int X11Grab_YUYVPseudo8MSB(uint8 *grabdata)
{
    int x, y;
    XColor *c0, *c1;
    uint8 *data=(uint8 *)ximage->image->data, *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;

    X11Grab_ComputeYUVTable();

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    c0 = &col[data[0]];
	    c1 = &col[data[1]];
	    data += 2;

	    y0 = ry[c0->red+c0->green+c0->blue];
	    y1 = ry[c1->red+c1->green+c1->blue];
	    uv = ruv[(((c0->red+c1->red) & 0xf800) +
		     ((c0->green+c1->green) & 0x7c0) +
		     ((c0->blue+c1->blue) & 0x3e))/2];

	    *yuyvp++ = (y0 << 24) + (y1 << 8) + uv;
	}
    }

    return 1;
}

static int X11Grab_YUYVPseudo8LSB(uint8 *grabdata)
{
    int x, y;
    XColor *c0, *c1;
    uint8 *data=(uint8 *)ximage->image->data, *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;

    X11Grab_ComputeYUVTable();

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    c0 = &col[data[0]];
	    c1 = &col[data[1]];
	    data += 2;

	    y0 = ry[c0->red+c0->green+c0->blue];
	    y1 = ry[c1->red+c1->green+c1->blue];
	    uv = ruv[(((c0->red+c1->red) & 0xf800) +
		     ((c0->green+c1->green) & 0x7c0) +
		     ((c0->blue+c1->blue) & 0x3e))/2];

	    *yuyvp++ = y0 + (y1 << 16) + uv;
	}
    }

    return 1;
}

static int X11Grab_GreyTrueXBGR24(uint8 *grabdata)
{
    int x, y;
    uint8 *yp=grabdata, *ry=rgb2y;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    yp[0] = ry[((p0<<7) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>19) & 0x1f)];
	    yp[1] = ry[((p1<<7) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>19) & 0x1f)];
	    yp += 2;
	}
    }

    return 1;
}

static int X11Grab_YUYVTrueXBGR24MSB(uint8 *grabdata)
{
    int x, y;
    uint8 *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    y0 = ry[((p0<<7) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>19) & 0x1f)];
	    y1 = ry[((p1<<7) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>19) & 0x1f)];

	    p0 &= 0xfefeff;
	    p1 &= 0xfefeff;
	    p0 += p1;
	    uv = ruv[((p0<<6)&0x7c00)+((p0>>7)&0x3e0)+((p0>>20)&0x1f)];

	    *yuyvp++ = (y0 << 24) + (y1 << 8) + uv;
	}
    }

    return 1;
}

static int X11Grab_YUYVTrueXBGR24LSB(uint8 *grabdata)
{
    int x, y;
    uint8 *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    y0 = ry[((p0<<7) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>19) & 0x1f)];
	    y1 = ry[((p1<<7) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>19) & 0x1f)];

	    p0 &= 0xfefeff;
	    p1 &= 0xfefeff;
	    p0 += p1;
	    uv = ruv[((p0<<6)&0x7c00)+((p0>>7)&0x3e0)+((p0>>20)&0x1f)];

	    *yuyvp++ = y0 + (y1 << 16) + uv;
	}
    }

    return 1;
}

static int X11Grab_GreyTrueXRGB24(uint8 *grabdata)
{
    int x, y;
    uint8 *yp=grabdata, *ry=rgb2y;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    yp[0] = ry[((p0>>9) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>3) & 0x1f)];
	    yp[1] = ry[((p1>>9) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>3) & 0x1f)];
	    yp += 2;
	}
    }

    return 1;
}

static int X11Grab_YUYVTrueXRGB24MSB(uint8 *grabdata)
{
    int x, y;
    uint8 *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    y0 = ry[((p0>>9) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>3) & 0x1f)];
	    y1 = ry[((p1>>9) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>3) & 0x1f)];

	    p0 &= 0xfefeff;
	    p1 &= 0xfefeff;
	    p0 += p1;
	    uv = ruv[((p0>>10)&0x7c00)+((p0>>7)&0x3e0)+((p0>>4)&0x1f)];

	    *yuyvp++ = (y0 << 24) + (y1 << 8) + uv;
	}
    }

    return 1;
}

static int X11Grab_YUYVTrueXRGB24LSB(uint8 *grabdata)
{
    int x, y;
    uint8 *ry=rgb2y;
    uint32 *yuyvp=(uint32 *)grabdata, *ruv=rgb2uv, y0, y1, uv;
    uint32 *data=(uint32 *)ximage->image->data, p0, p1;

    for (y=0; y<height; y++) {
	for (x=0; x<width; x+=2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;

	    y0 = ry[((p0>>9) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>3) & 0x1f)];
	    y1 = ry[((p1>>9) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>3) & 0x1f)];

	    p0 &= 0xfefeff;
	    p1 &= 0xfefeff;
	    p0 += p1;
	    uv = ruv[((p0>>10)&0x7c00)+((p0>>7)&0x3e0)+((p0>>4)&0x1f)];

	    *yuyvp++ = y0 + (y1 << 16) + uv;
	}
    }

    return 1;
}

static void X11Grab_SetGrabFunc(void)
{
    switch (root_depth) {
    case 1:
	if (white == 1) {
	    if (BitmapBitOrder(dpy) == LSBFirst) {
		grab = xmit_color? X11Grab_YUYVWhite1LSB
				 : X11Grab_GreyWhite1LSB;
	    } else {
		grab = xmit_color? X11Grab_YUYVWhite1MSB
				 : X11Grab_GreyWhite1MSB;
	    }
	} else {
	    if (BitmapBitOrder(dpy) == LSBFirst) {
		grab = xmit_color? X11Grab_YUYVBlack1LSB
				 : X11Grab_GreyBlack1LSB;
	    } else {
		grab = xmit_color? X11Grab_YUYVBlack1MSB
				 : X11Grab_GreyBlack1MSB;
	    }
	}
	break;
    case 8:
	switch (root_visinfo.class) {
	case PseudoColor:
	case GrayScale:
	case StaticColor:
	case StaticGray:
	    if (LITTLEENDIAN) {
		grab = xmit_color? X11Grab_YUYVPseudo8LSB
				 : X11Grab_GreyPseudo8;
	    } else {
		grab = xmit_color? X11Grab_YUYVPseudo8MSB
				 : X11Grab_GreyPseudo8;
	    }
	    break;
	default:
	    grab = NULL;
	    break;
	}
	break;
    case 24:
	if ((root_visinfo.class == TrueColor) &&
	    (root_visinfo.red_mask == 0xff) &&
	    (root_visinfo.green_mask = 0xff00) &&
	    (root_visinfo.blue_mask == 0xff0000)) {
	    if (LITTLEENDIAN) {
		grab = xmit_color? X11Grab_YUYVTrueXBGR24LSB
				 : X11Grab_GreyTrueXBGR24;
	    } else {
		grab = xmit_color? X11Grab_YUYVTrueXBGR24MSB
				 : X11Grab_GreyTrueXBGR24;
	    }
	} else if ((root_visinfo.class == TrueColor) &&
		   (root_visinfo.red_mask == 0xff0000) &&
		   (root_visinfo.green_mask = 0xff00) &&
		   (root_visinfo.blue_mask == 0xff)) {
	    if (LITTLEENDIAN) {
		grab = xmit_color? X11Grab_YUYVTrueXRGB24LSB
				 : X11Grab_GreyTrueXRGB24;
	    } else {
		grab = xmit_color? X11Grab_YUYVTrueXRGB24MSB
				 : X11Grab_GreyTrueXRGB24;
	    }
	} else {
	    grab = NULL;
	}
	break;
    default:
	grab = NULL;
	break;
    }
}

static void X11Grab_Initialize(Window rw, int w, int h)
{
    int i;
    XWindowAttributes wattr;

    if (root != rw) {
	root = rw;
	XGetWindowAttributes(dpy, root, &wattr);
	screen = XScreenNumberOfScreen(wattr.screen);
	colormap = DefaultColormapOfScreen(wattr.screen);
	ncolors = CellsOfScreen(wattr.screen);
	black = BlackPixelOfScreen(wattr.screen);
	white = WhitePixelOfScreen(wattr.screen);
	root_depth = wattr.depth;
	root_width = wattr.width;
	root_height = wattr.height;
	root_vis = wattr.visual;
	vRoot = VirtualRootWindow(dpy, screen);

	if (col != NULL) free(col);
	col = (XColor *) malloc(ncolors*sizeof(XColor));

	if (ximage != NULL) VidUtil_DestroyXImage(dpy, ximage);
	ximage = NULL;

	XMatchVisualInfo(dpy, screen, root_depth, root_vis->class,
			 &root_visinfo);
	X11Grab_SetGrabFunc();
    }

    if ((ximage == NULL) || (width != w) || (height != h)) {
	width = w;
	height = h;

	if (ximage != NULL) VidUtil_DestroyXImage(dpy, ximage);
	ximage = VidUtil_AllocXImage(dpy, root_vis, root_depth, w, h, False);
	
	if (grabdata != NULL) free(grabdata);
	grabdata = (uint8 *)malloc(xmit_color? w*h*2 : w*h);

	if (reconfig) (*reconfig)(enc_state, w, h);
    }
}

static void X11Grab_MakeBox(unsigned int x1, unsigned int y1,
			    unsigned int x2, unsigned int y2,
			    int *xp, int *yp, int *wp, int *hp)
{
    int w, h;

    w = x2-x1;
    if (w < 0) {
	*xp = x2;
	*wp = -w;
    } else {
	*xp = x1;
	*wp = w;
    }

    h = y2-y1;
    if (h < 0) {
	*yp = y2;
	*hp = -h;
    } else {
	*yp = y1;
	*hp = h;
    }
}

static int X11Grab_UpdatePos(Window rw, int x, int y, int w, int h)
{
    static char cmd[256];

    if (w < 8) w = 8;
    if (h < 8) h = 8;

    if (w > root_width/8*8) w = root_width/8*8;
    if (h > root_height/8*8) h = root_height/8*8;

    w = (w+7)/8*8;
    h = (h+7)/8*8;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (x > root_width-w) x = root_width-w;
    if (y > root_height-h) y = root_height-h;

    sprintf(cmd, "x11grabUpdatePos %d %d %d %d", x, y, w, h);
    (void) Tcl_Eval(interp, cmd);

    x_origin = x;
    y_origin = y;

    if ((root != rw) || (width != w) || (height != h)) {
	X11Grab_Initialize(rw, w, h);
	return 0;
    } else {
	return 1;
    }
}

static int X11Grab_FollowPointer(void)
{
    Window rw, cw;
    int x, y, wx, wy;
    unsigned int mask;

    XQueryPointer(dpy, root, &rw, &cw, &x, &y, &wx, &wy, &mask);

    if (x < x_origin+width/4) {
	x = x-width/4;
    } else if (x >= x_origin+3*width/4) {
	x = x-3*width/4;
    } else {
	x = x_origin;
    }

    if (y < y_origin+height/4) {
	y = y-height/4;
    } else if (y >= y_origin+3*height/4) {
	y = y-3*height/4;
    } else {
	y = y_origin;
    }

    return X11Grab_UpdatePos(rw, x, y, width, height);
}

static int X11Grab_FollowWindow(void)
{
    int x, y, w, h;
    XWindowAttributes wattr, vRoot_wattr;
    Tk_ErrorHandler handler;

    handler = Tk_CreateErrorHandler(dpy, -1, -1, -1, ErrHandler, NULL);
    xerror = 0;
    XGetWindowAttributes(dpy, target, &wattr);
    XSync(dpy, False);
    Tk_DeleteErrorHandler(handler);
    if ((target == None) || xerror) {
	target = None;
	(void) Tcl_Eval(interp,
	       ".grabControls.x11grab.row1.mode.window config -state disabled");
	(void) Tcl_Eval(interp, "set x11grabMode fixed");
	return 1;
    } else {
	XGetWindowAttributes(dpy, vRoot, &vRoot_wattr);
	x = wattr.x+vRoot_wattr.x;
	y = wattr.y+vRoot_wattr.y;
	w = wattr.width+2*wattr.border_width;
	h = wattr.height+2*wattr.border_width;

	return X11Grab_UpdatePos(root, x, y, w, h);
    }
}

static int X11Grab_CaptureFrame(uint8 **datap, int *lenp)
{
    int dograb;

    switch (mode) {
    case X11GRAB_FIXED:
	dograb = 1;
	break;
    case X11GRAB_POINTER:
	dograb = X11Grab_FollowPointer();
	break;
    case X11GRAB_WINDOW:
	dograb = X11Grab_FollowWindow();
	break;
    }

    if (dograb) {
	VidUtil_GetXImage(dpy, root, x_origin, y_origin, ximage);
	*datap = grabdata;
	*lenp = xmit_color? width*height*2 : width*height;
	return (*grab)(grabdata);
    } else {
	return 0;
    }
}

/*ARGSUSED*/
static char *X11Grab_TraceMode(ClientData clientData, Tcl_Interp *interp,
			       char *name1, char *name2, int flags)
{
    char *value;

    value = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (!strcmp(value, "fixed")) {
	mode = X11GRAB_FIXED;
    } else if (!strcmp(value, "pointer")) {
	mode = X11GRAB_POINTER;
    } else if (!strcmp(value, "window")) {
	if (target != None) {
	    mode = X11GRAB_WINDOW;
	} else {
	    (void) Tcl_Eval(interp, "set x11grabMode fixed");
	    mode = X11GRAB_FIXED;
	    return "x11grabSetMode: no target window";
	}
    } else {
	return "x11grabSetMode: invalid mode";
    }

    return NULL;
}

/*ARGSUSED*/
static int X11Grab_SetXCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[])
{
    int x;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " x\"", NULL);
	return TCL_ERROR;
    }
 
    x = atoi(argv[1]);
    (void) X11Grab_UpdatePos(root, x, y_origin, width, height);

    return TCL_OK;
}

/*ARGSUSED*/
static int X11Grab_SetYCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[])
{
    int y;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " y\"", NULL);
	return TCL_ERROR;
    }
 
    y = atoi(argv[1]);
    (void) X11Grab_UpdatePos(root, x_origin, y, width, height);

    return TCL_OK;
}

/*ARGSUSED*/
static int X11Grab_SetWCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[])
{
    int w;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " width\"", NULL);
	return TCL_ERROR;
    }
 
    w = atoi(argv[1]);
    (void) X11Grab_UpdatePos(root, x_origin, y_origin, w, height);

    return TCL_OK;
}

/*ARGSUSED*/
static int X11Grab_SetHCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[])
{
    int h;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " height\"", NULL);
	return TCL_ERROR;
    }
 
    h = atoi(argv[1]);
    (void) X11Grab_UpdatePos(root, x_origin, y_origin, width, h);

    return TCL_OK;
}

/*ARGSUSED*/
static int X11Grab_SetRegionCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{
    unsigned int rootx, rooty;
    int x, y, w, h, boxDrawn=0;
    GC xorGC;
    Cursor cursor;
    XEvent event;

    cursor = XCreateFontCursor(dpy, XC_cross);

    if (XGrabPointer(dpy, root, False, ButtonPressMask, GrabModeAsync,
		     GrabModeAsync, root, cursor, CurrentTime)!=GrabSuccess) {
	Tcl_AppendResult(interp, argv[0], ": can't grab mouse", NULL);
	return TCL_ERROR;
    }

    xorGC = XCreateGC(dpy, root, 0, NULL);
    XSetSubwindowMode(dpy, xorGC, IncludeInferiors);
    XSetForeground(dpy, xorGC, -1);
    XSetFunction(dpy, xorGC, GXxor);

    XMaskEvent(dpy, ButtonPressMask, &event);
    rootx = event.xbutton.x_root;
    rooty = event.xbutton.y_root;

    XChangeActivePointerGrab(dpy, ButtonMotionMask|ButtonReleaseMask, cursor,
			     CurrentTime);

    while (1) {
	XNextEvent(dpy, &event);
	switch (event.type) {
	case MotionNotify:
	    if (boxDrawn) {
		XDrawRectangle(dpy, root, xorGC, x, y, w, h);
		boxDrawn = 0;
	    }
	    while (XCheckTypedEvent(dpy, MotionNotify, &event)) ;
	    X11Grab_MakeBox(rootx, rooty, event.xbutton.x_root,
			    event.xbutton.y_root, &x, &y, &w, &h);
	    XDrawRectangle(dpy, root, xorGC, x, y, w, h);
	    boxDrawn = 1;
	    break;
	case ButtonRelease:
	    if (boxDrawn) {
		XDrawRectangle(dpy, root, xorGC, x, y, w, h);
		boxDrawn = 0;
	    }
	    XFlush(dpy);
	    X11Grab_MakeBox(rootx, rooty, event.xmotion.x_root,
			    event.xmotion.y_root, &x, &y, &w, &h);
	    XUngrabPointer(dpy, CurrentTime);
	    XFreeGC(dpy, xorGC);
	    XFreeCursor(dpy, cursor);
	    (void) Tcl_Eval(interp, "set x11grabMode fixed");
	    (void) X11Grab_UpdatePos(root, x, y, w, h);
	    return TCL_OK;
	}
    }
}

/*ARGSUSED*/
static int X11Grab_SetWindowCmd(ClientData clientData, Tcl_Interp *interp,
				int argc, char *argv[])
{
    int buttons=0;
    Cursor cursor;
    XEvent event;
 
    cursor = XCreateFontCursor(dpy, XC_crosshair);
    target = None;

    if (XGrabPointer(dpy, vRoot, False, ButtonPressMask|ButtonReleaseMask,
		     GrabModeSync, GrabModeAsync, root, cursor,
		     CurrentTime) != GrabSuccess) {
	Tcl_AppendResult(interp, argv[0], ": can't grab mouse", NULL);
	return TCL_ERROR;
    }
 
    while ((target == None) || (buttons != 0)) {
	XAllowEvents(dpy, SyncPointer, CurrentTime);
	XWindowEvent(dpy, vRoot, ButtonPressMask|ButtonReleaseMask, &event);
	switch (event.type) {
	case ButtonPress:
	    if (target == None) target = event.xbutton.subwindow;
	    buttons++;
	    break;
	case ButtonRelease:
	    if (buttons > 0) buttons--;
	    break;
	}
    }
 
    XUngrabPointer(dpy, CurrentTime);
    XFreeCursor(dpy, cursor);

    (void) Tcl_Eval(interp,
	       ".grabControls.x11grab.row1.mode.window config -state normal");
    (void) Tcl_Eval(interp, "set x11grabMode window");
    (void) X11Grab_FollowWindow();
    return TCL_OK;
}

int X11Grab_Probe(void)
{
    Window rw;

    if (tkMainWin == NULL) return 0;

    Tcl_TraceVar(interp, "x11grabMode", TCL_TRACE_WRITES, X11Grab_TraceMode,
		 NULL);
    Tcl_CreateCommand(interp, "x11grabSetX", X11Grab_SetXCmd, 0, NULL);
    Tcl_CreateCommand(interp, "x11grabSetY", X11Grab_SetYCmd, 0, NULL);
    Tcl_CreateCommand(interp, "x11grabSetW", X11Grab_SetWCmd, 0, NULL);
    Tcl_CreateCommand(interp, "x11grabSetH", X11Grab_SetHCmd, 0, NULL);
    Tcl_CreateCommand(interp, "x11grabSetRegion", X11Grab_SetRegionCmd, 0,
		      NULL);
    Tcl_CreateCommand(interp, "x11grabSetWindow", X11Grab_SetWindowCmd, 0,
		      NULL);

    dpy = Tk_Display(tkMainWin);
    rw = RootWindow(dpy, Tk_ScreenNumber(tkMainWin));

    X11Grab_Initialize(rw, width, height);

    if (ximage != NULL) {
	VidUtil_DestroyXImage(dpy, ximage);
	ximage = NULL;
    }

    if (grab != NULL)
	return VID_GREYSCALE|VID_COLOR|VID_SMALL|VID_MEDIUM|VID_LARGE;
    else
	return 0;
}

char *X11Grab_Attach(void)
{
    if (target == None)
	(void) Tcl_Eval(interp,
	       ".grabControls.x11grab.row1.mode.window config -state disabled");
    return ".grabControls.x11grab";
}

void X11Grab_Detach(void)
{
}

/*ARGSUSED*/
grabproc_t *X11Grab_Start(int grabtype, int min_framespacing, int config,
			  reconfigproc_t *r, void *e)
{
    int w=NTSC_WIDTH, h=NTSC_HEIGHT;

    xmit_size = (config & VID_SIZEMASK);
    reconfig = r;
    enc_state = e;

    width = height = 0;
    switch (xmit_size) {
    case VID_SMALL:
	w /= 2;
	h /= 2;
	break;
    case VID_MEDIUM:
	break;
    case VID_LARGE:
	w *= 2;
	h *= 2;
	break;
    }

    switch (grabtype) {
    case VIDIMAGE_GREY:
	xmit_color = 0;
	break;
    case VIDIMAGE_YUYV:
	xmit_color = 1;
	break;
    default:
	return NULL;
    }

    X11Grab_SetGrabFunc();
    X11Grab_UpdatePos(root, x_origin, y_origin, w, h);
    return X11Grab_CaptureFrame;
}

void X11Grab_Stop(void)
{
    VidUtil_DestroyXImage(dpy, ximage);
    ximage = NULL;
    reconfig = NULL;
    enc_state = NULL;
}
#endif /* X11GRAB */
