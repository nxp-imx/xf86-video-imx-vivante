/****************************************************************************
*
*    Copyright 2012 - 2017 Vivante Corporation, Santa Clara, California.
*    All Rights Reserved.
*
*    Permission is hereby granted, free of charge, to any person obtaining
*    a copy of this software and associated documentation files (the
*    'Software'), to deal in the Software without restriction, including
*    without limitation the rights to use, copy, modify, merge, publish,
*    distribute, sub license, and/or sell copies of the Software, and to
*    permit persons to whom the Software is furnished to do so, subject
*    to the following conditions:
*
*    The above copyright notice and this permission notice (including the
*    next paragraph) shall be included in all copies or substantial
*    portions of the Software.
*
*    THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
*    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
*    IN NO EVENT SHALL VIVANTE AND/OR ITS SUPPLIERS BE LIABLE FOR ANY
*    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
*    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
*    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/


#include "vivante_exa.h"

#include "vivante.h"

#include "vivante_priv.h"


#ifdef HAVE_G2D
/**
 *
 * @param pScreen
 * @param size
 * @param align
 * @return private vivpixmap
 */

void *
G2dVivCreatePixmap(ScreenPtr pScreen, int size, int align) {
    TRACE_ENTER();
    Viv2DPixmapPtr pVivPix = NULL;
    pVivPix = malloc(sizeof (Viv2DPixmap));
    if (!pVivPix) {
        TRACE_EXIT(NULL);
    }
    IGNORE(align);
    IGNORE(size);
    pVivPix->mVidMemInfo = NULL;
    pVivPix->mHWPath = FALSE;
    pVivPix->mCpuBusy = FALSE;
    pVivPix->mSwAnyWay = FALSE;
    pVivPix->mNextGpuBusyPixmap = NULL;
    pVivPix->mRef = 0;
    pVivPix->fdToPixmap = 0;
    TRACE_EXIT(pVivPix);
}

/**
 * Destroys the Pixmap
 * @param pScreen
 * @param dPriv
 */
void
G2dVivDestroyPixmap(ScreenPtr pScreen, void *dPriv) {
    TRACE_ENTER();
    Viv2DPixmapPtr priv = (Viv2DPixmapPtr) dPriv;
    VivPtr pViv = VIVPTR_FROM_SCREEN(pScreen);
    if (priv && (priv->mVidMemInfo)) {

        if (!DestroySurface(&pViv->mGrCtx, priv)) {
            TRACE_ERROR("Error on destroying the surface\n");
        }
        /*Removing the container*/
        free(priv);
        priv = NULL;
        dPriv = NULL;
    }
    TRACE_EXIT();
}

/**
 * PixmapIsOffscreen() is an optional driver replacement to
 * exaPixmapHasGpuCopy(). Set to NULL if you want the standard behaviour
 * of exaPixmapHasGpuCopy().
 *
 * @param pPix the pixmap
 * @return TRUE if the given drawable is in framebuffer memory.
 *
 * exaPixmapHasGpuCopy() is used to determine if a pixmap is in offscreen
 * memory, meaning that acceleration could probably be done to it, and that it
 * will need to be wrapped by PrepareAccess()/FinishAccess() when accessing it
 * with the CPU.
 */
Bool
G2dVivPixmapIsOffscreen(PixmapPtr pPixmap) {
    TRACE_ENTER();
    BOOL ret = FALSE;
    Viv2DPixmapPtr pVivPix = NULL;
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    pVivPix = (Viv2DPixmapPtr) exaGetPixmapDriverPrivate(pPixmap);

    /* offscreen means in 'gpu accessible memory', not that it's off the
    * visible screen.
    */
    if (pScreen->GetScreenPixmap(pScreen) == pPixmap) {
        TRACE_EXIT(TRUE);
    }

    if(pVivPix->mVidMemInfo!=NULL)
        ret = TRUE;
    else
        ret = pPixmap->devPrivate.ptr ? FALSE : TRUE;

    TRACE_EXIT(ret);
}

extern gctBOOL EXA_G2D;

/**
 * Returning a pixmap with non-NULL devPrivate.ptr implies a pixmap which is
 * not offscreen, which will never be accelerated and Prepare/FinishAccess won't
 * be called.
 */
Bool
G2dVivModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
    int depth, int bitsPerPixel, int devKind,
    pointer pPixData) {
    TRACE_ENTER();
    Bool ret = FALSE;
    Bool isChanged = FALSE;
    int prev_w = pPixmap->drawable.width;
    int prev_h = pPixmap->drawable.height;
    int prev_bpp = pPixmap->drawable.bitsPerPixel;
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPixmap);
    Viv2DPixmapPtr pVivPix = exaGetPixmapDriverPrivate(pPixmap);

    if (!pPixmap || !pVivPix) {
        TRACE_EXIT(FALSE);
    }
    ret = miModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData);
    if (!ret) {
         return ret;
    }

    if (depth <= 0) {
        depth = pPixmap->drawable.depth;
    }

    if (bitsPerPixel <= 0) {
        bitsPerPixel = pPixmap->drawable.bitsPerPixel;
    }

    if (width <= 0) {
        width = pPixmap->drawable.width;
    }

    if (height <= 0) {
        height = pPixmap->drawable.height;
    }

    if (width <= 0 || height <= 0 || depth <= 0) {
        TRACE_EXIT(FALSE);

    }

    isChanged = (!pVivPix->mVidMemInfo || prev_h != height || prev_w != width || (prev_bpp != bitsPerPixel && bitsPerPixel > 16));


    /* What is the start of screen (and offscreen) memory and its size. */

    CARD8* screenMemoryBegin =

    (CARD8*) (pViv->mFakeExa.mExaDriver->memoryBase);

    CARD8* screenMemoryEnd =screenMemoryBegin + pViv->mFakeExa.mExaDriver->memorySize;

    if ((screenMemoryBegin <= (CARD8*) (pPixData)) &&
        ((CARD8*) (pPixData) < screenMemoryEnd)) {

        /* Compute address relative to begin of FB memory. */

        const unsigned long offset =(CARD8*) (pPixData) - screenMemoryBegin;

        /* Store GPU address. */
        const unsigned long physical = pViv->mFB.memGpuBase + offset;

        pVivPix->fdToPixmap = 0;
        /* reserved buffers size is greater than current pixmap used. TODO: move shadow buffer out */
        if (!WrapSurface(pPixmap, pPixData, physical, pVivPix, pViv->mFakeExa.mExaDriver->memorySize/2)) {

            TRACE_ERROR("Frame Buffer Wrapping ERROR\n");
            TRACE_EXIT(FALSE);
        }
        TRACE_EXA("%s:%d VivPixmap:%p",__FUNCTION__,__LINE__,pVivPix);

        TRACE_EXIT(TRUE);

    }
#ifdef ENABLE_VIVANTE_DRI3
    else if (pPixData && bitsPerPixel > 8 && EXA_G2D) {
        struct g2d_buf *buf;

        buf = g2d_buf_from_virt_addr(pPixData, width*height*(bitsPerPixel/8));
        pVivPix->fdToPixmap = 0;
        if(!buf){
            pPixmap->devPrivate.ptr = pPixData;

            pPixmap->devKind = devKind;
            pVivPix->mVidMemInfo = NULL;

            TRACE_EXIT(FALSE);
        }

        if (!WrapSurface(pPixmap, pPixData, buf->buf_paddr, pVivPix, width*height*(bitsPerPixel/8))) {
            TRACE_ERROR("Frame Buffer Wrapping ERROR\n");

            TRACE_EXIT(FALSE);
        }
    }
#endif
    else if (pPixData) {

        TRACE_ERROR("NO ACCERELATION\n");

        /*No acceleration is avalaible*/

        pPixmap->devPrivate.ptr = pPixData;

        pPixmap->devKind = devKind;
        pVivPix->fdToPixmap = 0;
        TRACE_EXA("%s:%d VivPixmap:%p",__FUNCTION__,__LINE__,pVivPix);
        //VIV2DCacheOperation(&pViv->mGrCtx, pVivPix,FLUSH);
        //VIV2DGPUBlitComplete(&pViv->mGrCtx, TRUE);
        /*we never want to see this again*/
        if (!DestroySurface(&pViv->mGrCtx, pVivPix)) {

            TRACE_ERROR("ERROR : DestroySurface\n");
        }

        TRACE_EXA("%s:%d VivPixmap:%p",__FUNCTION__,__LINE__,pVivPix);
        pVivPix->mVidMemInfo = NULL;
        TRACE_EXIT(FALSE);

    } else {

        if (isChanged) {

#ifndef ENABLE_VIVANTE_DRI3
        pVivPix->fdToPixmap = 0;
#endif

        if (pVivPix->fdToPixmap) {
            if (!CreateSurfaceWithFd(&pViv->mGrCtx, pPixmap, pVivPix, pVivPix->fdToPixmap)) {
                TRACE_ERROR("ERROR : Creating the surface\n");
                fprintf(stderr,"CreateSurface failed\n");
                TRACE_EXIT(FALSE);
            }
            pVivPix->fdToPixmap = 0;
        } else {

            if (isChanged) {

                if ( !ReUseSurface(&pViv->mGrCtx, pPixmap, pVivPix) )
                {

                    if (!DestroySurface(&pViv->mGrCtx, pVivPix)) {
                        TRACE_ERROR("ERROR : Destroying the surface\n");
                        fprintf(stderr,"Destroy surface failed\n");
                        TRACE_EXIT(FALSE);
                    }

                    if (!CreateSurface(&pViv->mGrCtx, pPixmap, pVivPix)) {
                        TRACE_ERROR("ERROR : Creating the surface\n");
                        fprintf(stderr,"CreateSurface failed\n");
                        TRACE_EXIT(FALSE);
                    }
                }
            }
        }

        pPixmap->devKind = GetStride(pVivPix);

        /* Clean the new surface with black color in case the window gets scrambled image when the window is resized */
        if ( (pPixmap->drawable.width * pPixmap->drawable.height) > IMX_EXA_MIN_AREA_CLEAN )
        {
            CleanSurfaceBySW(&pViv->mGrCtx, pPixmap, pVivPix);
        }


        }

    }

    TRACE_EXIT(TRUE);
}

/**
 * PrepareAccess() is called before CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * PrepareAccess() will be called before CPU access to an offscreen pixmap.
 * This can be used to set up hardware surfaces for byteswapping or
 * untiling, or to adjust the pixmap's devPrivate.ptr for the purpose of
 * making CPU access use a different aperture.
 *
 * The index is one of #EXA_PREPARE_DEST, #EXA_PREPARE_SRC,
 * #EXA_PREPARE_MASK, #EXA_PREPARE_AUX_DEST, #EXA_PREPARE_AUX_SRC, or
 * #EXA_PREPARE_AUX_MASK. Since only up to #EXA_NUM_PREPARE_INDICES pixmaps
 * will have PrepareAccess() called on them per operation, drivers can have
 * a small, statically-allocated space to maintain state for PrepareAccess()
 * and FinishAccess() in.  Note that PrepareAccess() is only called once per
 * pixmap and operation, regardless of whether the pixmap is used as a
 * destination and/or source, and the index may not reflect the usage.
 *
 * PrepareAccess() may fail.  An example might be the case of hardware that
 * can set up 1 or 2 surfaces for CPU access, but not 3.  If PrepareAccess()
 * fails, EXA will migrate the pixmap to system memory.
 * DownloadFromScreen() must be implemented and must not fail if a driver
 * wishes to fail in PrepareAccess().  PrepareAccess() must not fail when
 * pPix is the visible screen, because the visible screen can not be
 * migrated.
 *
 * @return TRUE if PrepareAccess() successfully prepared the pixmap for CPU
 * drawing.
 * @return FALSE if PrepareAccess() is unsuccessful and EXA should use
 * DownloadFromScreen() to migrate the pixmap out.
 */
Bool
G2dVivPrepareAccess(PixmapPtr pPix, int index) {
    TRACE_ENTER();
    Viv2DPixmapPtr pVivPix = exaGetPixmapDriverPrivate(pPix);
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPix);
    VIV2DBLITINFOPTR pBlt = &pViv->mGrCtx.mBlitInfo;
    VIVGPUPtr pGpuCtx = (VIVGPUPtr)(pViv->mGrCtx.mGpu);

    if (pVivPix->mRef == 0) {
        pPix->devPrivate.ptr = MapSurface(pVivPix);
    }

    pVivPix->mRef++;

    TRACE_EXA("%s VivPixmap:%p",__FUNCTION__,pVivPix);
    if (pPix->devPrivate.ptr == NULL) {

        TRACE_EXA("Logical Address is not set\n");
        TRACE_EXIT(FALSE);

    }

    pVivPix->mCpuBusy=TRUE;
    TRACE_EXIT(TRUE);
}

/**
 * FinishAccess() is called after CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * FinishAccess() will be called after finishing CPU access of an offscreen
 * pixmap set up by PrepareAccess().  Note that the FinishAccess() will not be
 * called if PrepareAccess() failed and the pixmap was migrated out.
 */
void
G2dVivFinishAccess(PixmapPtr pPix, int index) {
    TRACE_ENTER();
    Viv2DPixmapPtr pVivPix = exaGetPixmapDriverPrivate(pPix);
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPix);
    IGNORE(index);

    TRACE_EXA("%s VivPixmap:%p",__FUNCTION__,pVivPix);

    if (pVivPix->mRef == 1) {

        pPix->devPrivate.ptr = NULL;

    }

    pVivPix->mRef--;
    TRACE_EXIT();
}

#endif

