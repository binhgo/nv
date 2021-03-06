/*
	Netvideo version 3.3
	Written by Ron Frederick <frederick@parc.xerox.com>

	Video utility routines
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

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifdef AIX
#include <net/nh.h>
#endif
#include <sys/ipc.h>
#include <X11/Xlib.h>
#include <tk.h>
#ifndef NO_SHM
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#endif
#include "sized_types.h"
#include "vid_util.h"

/* Sick little macro which will limit x to [0..255] with logical ops */
#define UCLIMIT(x) ((t = (x)), (t &= ~(t>>31)), (t | ~((t-256) >> 31)))
 
uint8 rgb2y[32768];
uint32 rgb2uv[32768];

static int shm_available, completion;

/*ARGSUSED*/
static int ErrHandler(ClientData clientData, XErrorEvent *errevp)
{
    shm_available = 0;
    return 0;
}

/*ARGSUSED*/
static Bool Completion(Display *dpy, XEvent *evp, char *arg)
{
    return evp->xany.type == completion;
}

/* Initialize the RGB555 to YUV tables */
static void VidUtil_InitRGBTables(void)
{
    int i, t, r, g, b, y, u, v;

    i = 0;
    for (r=4; r<256; r+=8) {
	for (g=4; g<256; g+=8) {
	    for (b=4; b<256; b+=8) {
		y = (38*r+75*g+15*b+64)/128;
		u = UCLIMIT(74*(b-y)/128+128);
		v = UCLIMIT(93*(r-y)/128+128);
		rgb2y[i] = y;
		if (LITTLEENDIAN) {
		    rgb2uv[i] = (((uint8)u) << 8) + (((uint8)v) << 24);
		} else {
		    rgb2uv[i] = (((uint8)u) << 16) + ((uint8)v);
		}
		i++;
	    }
	}
    }
}

void VidUtil_Init(Display *dpy)
{
#ifdef NO_SHM
    shm_available = 0;
#else
    if ((shm_available = XShmQueryExtension(dpy)) != 0)
	completion = XShmGetEventBase(dpy) + ShmCompletion;
#endif

    VidUtil_InitRGBTables();
}

ximage_t *VidUtil_AllocStdXImage(Display *dpy, Visual *vis, int depth,
				 int width, int height)
{
    ximage_t *ximage;
    int ximage_size, pad;

    ximage = (ximage_t *) malloc(sizeof(ximage_t));
    if (ximage == NULL) return NULL;

    switch (depth) {
    case 1:
	pad = 8;
	break;
    case 24:
	pad = 32;
	break;
    default:
	pad = depth;
	break;
    }
	
    ximage->image = XCreateImage(dpy, vis, depth, ZPixmap, 0, NULL, width,
				 height, pad, 0);
    ximage_size = ximage->image->bytes_per_line * ximage->image->height;
    ximage->image->data = (char *) malloc(ximage_size);

    ximage->shminfo = NULL;

    return ximage;
}

ximage_t *VidUtil_AllocXImage(Display *dpy, Visual *vis, int depth, int width,
			      int height, int readonly)
{
    ximage_t *ximage;
    int ximage_size, pad;
    Tk_ErrorHandler handler;

    ximage = (ximage_t *) malloc(sizeof(ximage_t));
    if (ximage == NULL) return NULL;

    if (shm_available) {
#ifndef NO_SHM
	XShmSegmentInfo *shminfo;

	ximage->shminfo = shminfo =
	    (XShmSegmentInfo *) malloc(sizeof(XShmSegmentInfo));

	ximage->image = XShmCreateImage(dpy, vis, depth, ZPixmap, 0, shminfo,
					width, height);
	ximage_size = ximage->image->bytes_per_line * ximage->image->height;

	shminfo->shmid = shmget(IPC_PRIVATE, ximage_size, IPC_CREAT|0777);
	shm_available = (shminfo->shmid >= 0);
	if (shm_available) {
	    shminfo->shmaddr = ximage->image->data =
		(char *) shmat(shminfo->shmid, 0, 0);
	    shminfo->readOnly = readonly;

	    handler = Tk_CreateErrorHandler(dpy, -1, -1, -1, ErrHandler, NULL);
	    XShmAttach(dpy, shminfo);
	    XSync(dpy, False);
	    Tk_DeleteErrorHandler(handler);

	    if (!shm_available) {
		shmdt(shminfo->shmaddr);
		shmctl(shminfo->shmid, IPC_RMID, 0);
		XDestroyImage(ximage->image);
		free(shminfo);
	    }
	} else {
	    XDestroyImage(ximage->image);
	    free(shminfo);
	}
#endif
    }

    if (!shm_available) {
	switch (depth) {
	case 1:
	    pad = 8;
	    break;
	case 24:
	    pad = 32;
	    break;
	default:
	    pad = depth;
	    break;
	}
	    
	ximage->image = XCreateImage(dpy, vis, depth, ZPixmap, 0, NULL, width,
				     height, pad, 0);
	ximage_size = ximage->image->bytes_per_line * ximage->image->height;
	ximage->image->data = (char *) malloc(ximage_size);

	ximage->shminfo = NULL;
    }

    return ximage;
}

void VidUtil_DestroyXImage(Display *dpy, ximage_t *ximage)
{
#ifndef NO_SHM
    if (ximage->shminfo != NULL) {
	XShmSegmentInfo *shminfo=ximage->shminfo;

	XShmDetach(dpy, shminfo);
	shmdt(shminfo->shmaddr);
	shmctl(shminfo->shmid, IPC_RMID, 0);
	free(shminfo);
    }
#endif

    XDestroyImage(ximage->image);
    free(ximage);
}

void VidUtil_GetXImage(Display *dpy, Window w, int x, int y, ximage_t *ximage)
{
    XImage *image=ximage->image;

#ifndef NO_SHM
    if (ximage->shminfo != NULL) {
	XShmGetImage(dpy, w, image, x, y, AllPlanes);
    } else
#endif
    {
	XGetSubImage(dpy, w, x, y, image->width, image->height, AllPlanes,
		     ZPixmap, image, 0, 0);
    }
}

void VidUtil_PutXImage(Display *dpy, Window w, GC gc, int x, int y,
		       ximage_t *ximage)
{
    XImage *image=ximage->image;
    XEvent event;

#ifndef NO_SHM
    if (ximage->shminfo != NULL) {
	XShmPutImage(dpy, w, gc, image, x, y, 0, 0, image->width,
		     image->height, True);
	XIfEvent(dpy, &event, Completion, NULL);
    } else
#endif
    {
	XPutImage(dpy, w, gc, image, x, y, 0, 0, image->width, image->height);
    }
}
