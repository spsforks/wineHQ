/*
 * X11 graphics driver initialisation functions
 *
 * Copyright 1996 Alexandre Julliard
 */

#include "config.h"

#include "ts_xlib.h"

#include <string.h>

#include "bitmap.h"
#include "color.h"
#include "debugtools.h"
#include "winnt.h"
#include "x11drv.h"
#include "ddrawi.h"

DEFAULT_DEBUG_CHANNEL(x11drv);

static BOOL X11DRV_CreateDC( DC *dc, LPCSTR driver, LPCSTR device,
                               LPCSTR output, const DEVMODEA* initData );
static BOOL X11DRV_DeleteDC( DC *dc );

static INT X11DRV_Escape( DC *dc, INT nEscape, INT cbInput,
                            SEGPTR lpInData, SEGPTR lpOutData );

const DC_FUNCTIONS X11DRV_DC_Funcs =
{
    NULL,                            /* pAbortDoc */
    NULL,                            /* pAbortPath */
    NULL,                            /* pAngleArc */
    X11DRV_Arc,                      /* pArc */
    NULL,                            /* pArcTo */
    NULL,                            /* pBeginPath */
    X11DRV_BitBlt,                   /* pBitBlt */
    X11DRV_BitmapBits,               /* pBitmapBits */
    X11DRV_ChoosePixelFormat,        /* pChoosePixelFormat */
    X11DRV_Chord,                    /* pChord */
    NULL,                            /* pCloseFigure */
    X11DRV_CreateBitmap,             /* pCreateBitmap */
    X11DRV_CreateDC,                 /* pCreateDC */
    X11DRV_DIB_CreateDIBSection,     /* pCreateDIBSection */
    X11DRV_DIB_CreateDIBSection16,   /* pCreateDIBSection16 */
    X11DRV_DeleteDC,                 /* pDeleteDC */
    X11DRV_DeleteObject,             /* pDeleteObject */
    X11DRV_DescribePixelFormat,      /* pDescribePixelFormat */
    NULL,                            /* pDeviceCapabilities */
    X11DRV_Ellipse,                  /* pEllipse */
    NULL,                            /* pEndDoc */
    NULL,                            /* pEndPage */
    NULL,                            /* pEndPath */
    X11DRV_EnumDeviceFonts,          /* pEnumDeviceFonts */
    X11DRV_Escape,                   /* pEscape */
    NULL,                            /* pExcludeClipRect */
    NULL,                            /* pExtDeviceMode */
    X11DRV_ExtFloodFill,             /* pExtFloodFill */
    X11DRV_ExtTextOut,               /* pExtTextOut */
    NULL,                            /* pFillPath */
    NULL,                            /* pFillRgn */
    NULL,                            /* pFlattenPath */
    NULL,                            /* pFrameRgn */
    X11DRV_GetCharWidth,             /* pGetCharWidth */
    X11DRV_GetDCOrgEx,               /* pGetDCOrgEx */
    X11DRV_GetDeviceGammaRamp,       /* pGetDeviceGammaRamp */
    X11DRV_GetPixel,                 /* pGetPixel */
    X11DRV_GetPixelFormat,           /* pGetPixelFormat */
    X11DRV_GetTextExtentPoint,       /* pGetTextExtentPoint */
    X11DRV_GetTextMetrics,           /* pGetTextMetrics */
    NULL,                            /* pIntersectClipRect */
    NULL,                            /* pInvertRgn */
    X11DRV_LineTo,                   /* pLineTo */
    NULL,                            /* pMoveTo */
    NULL,                            /* pOffsetClipRgn */
    NULL,                            /* pOffsetViewportOrg (optional) */
    NULL,                            /* pOffsetWindowOrg (optional) */
    X11DRV_PaintRgn,                 /* pPaintRgn */
    X11DRV_PatBlt,                   /* pPatBlt */
    X11DRV_Pie,                      /* pPie */
    NULL,                            /* pPolyBezier */
    NULL,                            /* pPolyBezierTo */
    NULL,                            /* pPolyDraw */
    X11DRV_PolyPolygon,              /* pPolyPolygon */
    X11DRV_PolyPolyline,             /* pPolyPolyline */
    X11DRV_Polygon,                  /* pPolygon */
    X11DRV_Polyline,                 /* pPolyline */
    NULL,                            /* pPolylineTo */
    NULL,                            /* pRealizePalette */
    X11DRV_Rectangle,                /* pRectangle */
    NULL,                            /* pRestoreDC */
    X11DRV_RoundRect,                /* pRoundRect */
    NULL,                            /* pSaveDC */
    NULL,                            /* pScaleViewportExt (optional) */
    NULL,                            /* pScaleWindowExt (optional) */
    NULL,                            /* pSelectClipPath */
    NULL,                            /* pSelectClipRgn */
    X11DRV_SelectObject,             /* pSelectObject */
    NULL,                            /* pSelectPalette */
    X11DRV_SetBkColor,               /* pSetBkColor */
    NULL,                            /* pSetBkMode */
    X11DRV_SetDeviceClipping,        /* pSetDeviceClipping */
    X11DRV_SetDeviceGammaRamp,       /* pSetDeviceGammaRamp */
    X11DRV_SetDIBitsToDevice,        /* pSetDIBitsToDevice */
    NULL,                            /* pSetMapMode (optional) */
    NULL,                            /* pSetMapperFlags */
    X11DRV_SetPixel,                 /* pSetPixel */
    X11DRV_SetPixelFormat,           /* pSetPixelFormat */
    NULL,                            /* pSetPolyFillMode */
    NULL,                            /* pSetROP2 */
    NULL,                            /* pSetRelAbs */
    NULL,                            /* pSetStretchBltMode */
    NULL,                            /* pSetTextAlign */
    NULL,                            /* pSetTextCharacterExtra */
    X11DRV_SetTextColor,             /* pSetTextColor */
    NULL,                            /* pSetTextJustification */
    NULL,                            /* pSetViewportExt (optional) */
    NULL,                            /* pSetViewportOrg (optional) */
    NULL,                            /* pSetWindowExt (optional) */
    NULL,                            /* pSetWindowOrg (optional) */
    NULL,                            /* pStartDoc */
    NULL,                            /* pStartPage */
    X11DRV_StretchBlt,               /* pStretchBlt */
    NULL,                            /* pStretchDIBits */
    NULL,                            /* pStrokeAndFillPath */
    NULL,                            /* pStrokePath */
    X11DRV_SwapBuffers,              /* pSwapBuffers */
    NULL                             /* pWidenPath */
};

BITMAP_DRIVER X11DRV_BITMAP_Driver =
{
  X11DRV_DIB_SetDIBits,
  X11DRV_DIB_GetDIBits,
  X11DRV_DIB_DeleteDIBSection,
  X11DRV_DIB_SetDIBColorTable,
  X11DRV_DIB_GetDIBColorTable,
  X11DRV_DIB_Lock,
  X11DRV_DIB_Unlock
};

PALETTE_DRIVER X11DRV_PALETTE_Driver =
{
  X11DRV_PALETTE_SetMapping,
  X11DRV_PALETTE_UpdateMapping,
  X11DRV_PALETTE_IsDark
};

DeviceCaps X11DRV_DevCaps = {
/* version */		0, 
/* technology */	DT_RASDISPLAY,
/* size, resolution */	0, 0, 0, 0, 0, 
/* device objects */	1, -1, -1, 0, 0, -1, 1152,	
/* curve caps */	CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES |
			CC_WIDE | CC_STYLED | CC_WIDESTYLED | CC_INTERIORS | CC_ROUNDRECT,
/* line caps */		LC_POLYLINE | LC_MARKER | LC_POLYMARKER | LC_WIDE |
			LC_STYLED | LC_WIDESTYLED | LC_INTERIORS,
/* polygon caps */	PC_POLYGON | PC_RECTANGLE | PC_WINDPOLYGON |
			PC_SCANLINE | PC_WIDE | PC_STYLED | PC_WIDESTYLED | PC_INTERIORS,
/* text caps */		0,
/* regions */		CP_REGION,
/* raster caps */	RC_BITBLT | RC_BANDING | RC_SCALING | RC_BITMAP64 |
			RC_DI_BITMAP | RC_DIBTODEV | RC_BIGFONT | RC_STRETCHBLT | RC_STRETCHDIB | RC_DEVBITS,
/* aspects */		36, 36, 51,
/* pad1 */		{ 0 },
/* log pixels */	0, 0, 
/* pad2 */		{ 0 },
/* palette size */	0,
/* ..etc */		0, 0 };


Display *gdi_display;  /* display to use for all GDI functions */


/**********************************************************************
 *	     X11DRV_GDI_Initialize
 */
BOOL X11DRV_GDI_Initialize( Display *display )
{
    Screen *screen = DefaultScreenOfDisplay(display);

    gdi_display = display;
    BITMAP_Driver = &X11DRV_BITMAP_Driver;
    PALETTE_Driver = &X11DRV_PALETTE_Driver;

    /* FIXME: colormap management should be merged with the X11DRV */

    if( !X11DRV_PALETTE_Init() ) return FALSE;

    if( !X11DRV_OBM_Init() ) return FALSE;

    /* Finish up device caps */

    X11DRV_DevCaps.version   = 0x300;
    X11DRV_DevCaps.horzSize  = WidthMMOfScreen(screen) * screen_width / WidthOfScreen(screen);
    X11DRV_DevCaps.vertSize  = HeightMMOfScreen(screen) * screen_height / HeightOfScreen(screen);
    X11DRV_DevCaps.horzRes   = screen_width;
    X11DRV_DevCaps.vertRes   = screen_height;
    X11DRV_DevCaps.bitsPixel = screen_depth;

    /* MSDN: Number of entries in the device's color table, if the device has
     * a color depth of no more than 8 bits per pixel.For devices with greater
     * color depths, -1 is returned.
     */
    X11DRV_DevCaps.numColors = (screen_depth>8)?-1:(1<<screen_depth);
 
    /* Resolution will be adjusted during the font init */

    X11DRV_DevCaps.logPixelsX = (int)(X11DRV_DevCaps.horzRes * 25.4 / X11DRV_DevCaps.horzSize);
    X11DRV_DevCaps.logPixelsY = (int)(X11DRV_DevCaps.vertRes * 25.4 / X11DRV_DevCaps.vertSize);

    /* Create default bitmap */

    if (!X11DRV_BITMAP_Init()) return FALSE;

    /* Initialize fonts and text caps */

    if (!X11DRV_FONT_Init( &X11DRV_DevCaps )) return FALSE;

    return DRIVER_RegisterDriver( "DISPLAY", &X11DRV_DC_Funcs );
}

/**********************************************************************
 *	     X11DRV_GDI_Finalize
 */
void X11DRV_GDI_Finalize(void)
{
    X11DRV_PALETTE_Cleanup();
    XCloseDisplay( gdi_display );
    gdi_display = NULL;
}

/**********************************************************************
 *	     X11DRV_CreateDC
 */
static BOOL X11DRV_CreateDC( DC *dc, LPCSTR driver, LPCSTR device,
                               LPCSTR output, const DEVMODEA* initData )
{
    X11DRV_PDEVICE *physDev;

    dc->physDev = physDev = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY,
				       sizeof(*physDev) );
    if(!physDev) {
        ERR("Can't allocate physDev\n");
	return FALSE;
    }

    dc->devCaps      = &X11DRV_DevCaps;
    if (dc->flags & DC_MEMORY)
    {
        BITMAPOBJ *bmp = (BITMAPOBJ *) GDI_GetObjPtr( dc->hBitmap, BITMAP_MAGIC );
	if (!bmp) 
	{
	    HeapFree( GetProcessHeap(), 0, physDev );
	    return FALSE;
        }
        if (!bmp->physBitmap) X11DRV_CreateBitmap( dc->hBitmap );
        physDev->drawable  = (Pixmap)bmp->physBitmap;
        physDev->gc        = TSXCreateGC( gdi_display, physDev->drawable, 0, NULL );
        dc->bitsPerPixel       = bmp->bitmap.bmBitsPixel;
        dc->totalExtent.left   = 0;
        dc->totalExtent.top    = 0;
        dc->totalExtent.right  = bmp->bitmap.bmWidth;
        dc->totalExtent.bottom = bmp->bitmap.bmHeight;
        GDI_ReleaseObj( dc->hBitmap );
    }
    else
    {
        physDev->drawable  = root_window;
        physDev->gc        = TSXCreateGC( gdi_display, physDev->drawable, 0, NULL );
        dc->bitsPerPixel       = screen_depth;
        dc->totalExtent.left   = 0;
        dc->totalExtent.top    = 0;
        dc->totalExtent.right  = screen_width;
        dc->totalExtent.bottom = screen_height;
    }

    physDev->current_pf   = 0;
    physDev->used_visuals = 0;

    if (!(dc->hVisRgn = CreateRectRgnIndirect( &dc->totalExtent )))
    {
        TSXFreeGC( gdi_display, physDev->gc );
	HeapFree( GetProcessHeap(), 0, physDev );
        return FALSE;
    }

    wine_tsx11_lock();
    XSetGraphicsExposures( gdi_display, physDev->gc, False );
    XSetSubwindowMode( gdi_display, physDev->gc, IncludeInferiors );
    XFlush( gdi_display );
    wine_tsx11_unlock();
    return TRUE;
}


/**********************************************************************
 *	     X11DRV_DeleteDC
 */
static BOOL X11DRV_DeleteDC( DC *dc )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    wine_tsx11_lock();
    XFreeGC( gdi_display, physDev->gc );
    while (physDev->used_visuals-- > 0)
        XFree(physDev->visuals[physDev->used_visuals]);
    wine_tsx11_unlock();
    HeapFree( GetProcessHeap(), 0, physDev );
    dc->physDev = NULL;
    return TRUE;
}

/**********************************************************************
 *           X11DRV_Escape
 */
static INT X11DRV_Escape( DC *dc, INT nEscape, INT cbInput,
                            SEGPTR lpInData, SEGPTR lpOutData )
{
    switch( nEscape )
    {
	case QUERYESCSUPPORT:
	     if( lpInData )
	     {
		 LPINT16 lpEscape = MapSL(lpInData);
		 switch (*lpEscape)
		 {
		     case DCICOMMAND:
			 return DD_HAL_VERSION;
		 }
	     }
	     break;

	case GETSCALINGFACTOR:
	     if( lpOutData )
	     {
		 LPPOINT16 lppt = MapSL(lpOutData);
		 lppt->x = lppt->y = 0;	/* no device scaling */
		 return 1;
	     }
	     break;

	case DCICOMMAND:
	     if( lpInData )
	     {
		 LPDCICMD lpCmd = MapSL(lpInData);
		 if (lpCmd->dwVersion != DD_VERSION) break;
		 return X11DRV_DCICommand(cbInput, lpCmd, MapSL(lpOutData));
	     }
	     break;

    }
    return 0;
}
