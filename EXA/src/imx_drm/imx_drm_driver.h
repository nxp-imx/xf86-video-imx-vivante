/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Author: Alan Hourihane <alanh@tungstengraphics.com>
 *
 */

#include <errno.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86Crtc.h>
#include <damage.h>


#include "drmmode_display.h"
#define DRV_ERROR(msg)	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, msg);
#define MS_LOGLEVEL_DEBUG 4



typedef struct
{
    int fd;
    int fd_ref;
    unsigned long fd_wakeup_registered; /* server generation for which fd has been registered for wakeup handling */
    int fd_wakeup_ref;
    unsigned int assigned_crtcs;
} modesettingEntRec, *modesettingEntPtr;


typedef void (*ms_drm_handler_proc)(uint64_t frame,
                                    uint64_t usec,
                                    void *data);

typedef void (*ms_drm_abort_proc)(void *data);

/**
 * A tracked handler for an event that will hopefully be generated by
 * the kernel, and what to do when it is encountered.
 */
struct ms_drm_queue {
    struct xorg_list list;
    xf86CrtcPtr crtc;
    uint32_t seq;
    void *data;
    ScrnInfoPtr scrn;
    ms_drm_handler_proc handler;
    ms_drm_abort_proc abort;
};

modesettingEntPtr ms_ent_priv(ScrnInfoPtr scrn);

uint32_t ms_drm_queue_alloc(xf86CrtcPtr crtc,
                            void *data,
                            ms_drm_handler_proc handler,
                            ms_drm_abort_proc abort);

void ms_drm_abort(ScrnInfoPtr scrn,
                  Bool (*match)(void *data, void *match_data),
                  void *match_data);
void ms_drm_abort_seq(ScrnInfoPtr scrn, uint32_t seq);

Bool ms_crtc_on(xf86CrtcPtr crtc);

xf86CrtcPtr ms_covering_crtc(ScrnInfoPtr scrn, BoxPtr box,
                             xf86CrtcPtr desired, BoxPtr crtc_box_ret);

int ms_get_crtc_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc);

uint32_t ms_crtc_msc_to_kernel_msc(xf86CrtcPtr crtc, uint64_t expect);
uint64_t ms_kernel_msc_to_crtc_msc(xf86CrtcPtr crtc, uint32_t sequence);

Bool ms_vblank_screen_init(ScreenPtr screen);
void ms_vblank_close_screen(ScreenPtr screen);

Bool ms_present_screen_init(ScreenPtr screen);
