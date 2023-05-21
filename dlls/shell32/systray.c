/*
 * Systray handling
 *
 * Copyright 1999 Kai Morich	<kai.morich@bigfoot.de>
 * Copyright 2004 Mike Hearn, for CodeWeavers
 * Copyright 2005 Robert Shearman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winnls.h"
#include "winuser.h"
#include "shellapi.h"
#include "shell32_main.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(systray);

struct notify_data_icon
{
    /* data for the icon bitmap */
    UINT width;
    UINT height;
    UINT planes;
    UINT bpp;
    char buffer[];
};

struct notify_data  /* platform-independent format for NOTIFYICONDATA */
{
    LONG  hWnd;
    UINT  uID;
    UINT  uFlags;
    UINT  uCallbackMessage;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union {
        UINT uTimeout;
        UINT uVersion;
    } u;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID  guidItem;
    struct notify_data_icon icons[];
};


/*************************************************************************
 * Shell_NotifyIcon			[SHELL32.296]
 * Shell_NotifyIconA			[SHELL32.297]
 */
BOOL WINAPI Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA pnid)
{
    NOTIFYICONDATAW nidW;
    INT cbSize;

    /* Validate the cbSize as Windows XP does */
    if (pnid->cbSize != NOTIFYICONDATAA_V1_SIZE &&
        pnid->cbSize != NOTIFYICONDATAA_V2_SIZE &&
        pnid->cbSize != NOTIFYICONDATAA_V3_SIZE &&
        pnid->cbSize != sizeof(NOTIFYICONDATAA))
    {
        WARN("Invalid cbSize (%ld) - using only Win95 fields (size=%ld)\n",
            pnid->cbSize, NOTIFYICONDATAA_V1_SIZE);
        cbSize = NOTIFYICONDATAA_V1_SIZE;
    }
    else
        cbSize = pnid->cbSize;

    ZeroMemory(&nidW, sizeof(nidW));
    nidW.cbSize = sizeof(nidW);
    nidW.hWnd   = pnid->hWnd;
    nidW.uID    = pnid->uID;
    nidW.uFlags = pnid->uFlags;
    nidW.uCallbackMessage = pnid->uCallbackMessage;
    nidW.hIcon  = pnid->hIcon;

    /* szTip */
    if (pnid->uFlags & NIF_TIP)
        MultiByteToWideChar(CP_ACP, 0, pnid->szTip, -1, nidW.szTip, ARRAY_SIZE(nidW.szTip));

    if (cbSize >= NOTIFYICONDATAA_V2_SIZE)
    {
        nidW.dwState      = pnid->dwState;
        nidW.dwStateMask  = pnid->dwStateMask;

        /* szInfo, szInfoTitle */
        if (pnid->uFlags & NIF_INFO)
        {
            MultiByteToWideChar(CP_ACP, 0, pnid->szInfo, -1,  nidW.szInfo, ARRAY_SIZE(nidW.szInfo));
            MultiByteToWideChar(CP_ACP, 0, pnid->szInfoTitle, -1, nidW.szInfoTitle, ARRAY_SIZE(nidW.szInfoTitle));
        }

        nidW.uTimeout = pnid->uTimeout;
        nidW.dwInfoFlags = pnid->dwInfoFlags;
    }
    
    if (cbSize >= NOTIFYICONDATAA_V3_SIZE)
        nidW.guidItem = pnid->guidItem;

    if (cbSize >= sizeof(NOTIFYICONDATAA))
        nidW.hBalloonIcon = pnid->hBalloonIcon;
    return Shell_NotifyIconW(dwMessage, &nidW);
}

/*************************************************************************
 * Shell_NotifyIconW			[SHELL32.298]
 */
BOOL WINAPI Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW nid)
{
    HWND tray;
    COPYDATASTRUCT cds;
    struct notify_data data_buffer;
    struct notify_data *data = &data_buffer;
    ICONINFO iconinfo[2] = { { 0 }, { 0 } };
    BITMAP bmMask[2];
    BITMAP bmColour[2];
    LONG cbMaskBits[2] = { 0, 0};
    LONG cbColourBits[2] = { 0, 0 };
    LONG iconCount = 0;
    BOOL ret;

    TRACE("dwMessage = %ld, nid->cbSize=%ld\n", dwMessage, nid->cbSize);

    /* Validate the cbSize so that WM_COPYDATA doesn't crash the application */
    if (nid->cbSize != NOTIFYICONDATAW_V1_SIZE &&
        nid->cbSize != NOTIFYICONDATAW_V2_SIZE &&
        nid->cbSize != NOTIFYICONDATAW_V3_SIZE &&
        nid->cbSize != sizeof(NOTIFYICONDATAW))
    {
        NOTIFYICONDATAW newNid;

        WARN("Invalid cbSize (%ld) - using only Win95 fields (size=%ld)\n",
            nid->cbSize, NOTIFYICONDATAW_V1_SIZE);
        CopyMemory(&newNid, nid, NOTIFYICONDATAW_V1_SIZE);
        newNid.cbSize = NOTIFYICONDATAW_V1_SIZE;
        return Shell_NotifyIconW(dwMessage, &newNid);
    }

    tray = FindWindowExW(0, NULL, L"Shell_TrayWnd", NULL);
    if (!tray)
    {
        SetLastError(E_FAIL);
        return FALSE;
    }

    cds.dwData = dwMessage;
    cds.cbData = sizeof(*data);
    memset( data, 0, sizeof(*data) );

    /* FIXME: if statement only needed because we don't support interprocess
     * icon handles */
    if (nid->uFlags & NIF_ICON)
    {
        if (!GetIconInfo(nid->hIcon, &iconinfo[iconCount]))
            goto noicon;

        if (!GetObjectW(iconinfo[iconCount].hbmMask, sizeof(bmMask), &bmMask[iconCount]) ||
            (iconinfo[iconCount].hbmColor && !GetObjectW(iconinfo[iconCount].hbmColor, sizeof(bmColour), &bmColour[iconCount])))
        {
            DeleteObject(iconinfo[iconCount].hbmMask);
            if (iconinfo[iconCount].hbmColor) DeleteObject(iconinfo[iconCount].hbmColor);
            goto noicon;
        }
        cbMaskBits[iconCount] = (bmMask[iconCount].bmPlanes * bmMask[iconCount].bmWidth * bmMask[iconCount].bmHeight * bmMask[iconCount].bmBitsPixel + 15) / 16 * 2;
        if (iconinfo[iconCount].hbmColor)
            cbColourBits[iconCount] = (bmColour[iconCount].bmPlanes * bmColour[iconCount].bmWidth * bmColour[iconCount].bmHeight * bmColour[iconCount].bmBitsPixel + 15) / 16 * 2;
        iconCount++;
    }
noicon:
    if (nid->uFlags & NIF_INFO)
    {
        if (!GetIconInfo(nid->hBalloonIcon, &iconinfo[iconCount]))
            goto noballoon;

        if (!GetObjectW(iconinfo[iconCount].hbmMask, sizeof(bmMask), &bmMask[iconCount]) ||
            (iconinfo[iconCount].hbmColor && !GetObjectW(iconinfo[iconCount].hbmColor, sizeof(bmColour), &bmColour[iconCount])))
        {
            DeleteObject(iconinfo[iconCount].hbmMask);
            if (iconinfo[iconCount].hbmColor) DeleteObject(iconinfo[iconCount].hbmColor);
            goto noballoon;
        }
        cbMaskBits[iconCount] = (bmMask[iconCount].bmPlanes * bmMask[iconCount].bmWidth * bmMask[iconCount].bmHeight * bmMask[iconCount].bmBitsPixel + 15) / 16 * 2;
        if (iconinfo[iconCount].hbmColor)
            cbColourBits[iconCount] = (bmColour[iconCount].bmPlanes * bmColour[iconCount].bmWidth * bmColour[iconCount].bmHeight * bmColour[iconCount].bmBitsPixel + 15) / 16 * 2;
        iconCount++;
    }
noballoon:
    if (iconCount > 0)
    {
        char *buffer;
        struct notify_data_icon *data_icon;
        cds.cbData = sizeof(*data) + sizeof(struct notify_data_icon) * iconCount + cbMaskBits[0] + cbMaskBits[1] + cbColourBits[0] + cbColourBits[1];
        buffer = malloc(cds.cbData);
        if (!buffer)
        {
            for (unsigned int i = 0; i < iconCount; i++)
            {
                if (iconinfo[i].hbmMask) DeleteObject(iconinfo[i].hbmMask);
                if (iconinfo[i].hbmColor) DeleteObject(iconinfo[i].hbmColor);
            }
            SetLastError(E_OUTOFMEMORY);
            return FALSE;
        }

        data = (struct notify_data *)buffer;
        data_icon = &data->icons[0];
        memset( data, 0, sizeof(*data) );
        for (unsigned int i = 0; i < iconCount; i++)
        {
            GetBitmapBits(iconinfo[i].hbmMask, cbMaskBits[i], &data_icon->buffer);
            if (!iconinfo[i].hbmColor)
            {
                data_icon->width  = bmMask[i].bmWidth;
                data_icon->height = bmMask[i].bmHeight / 2;
                data_icon->planes = 1;
                data_icon->bpp    = 1;
            }
            else
            {
                data_icon->width  = bmColour[i].bmWidth;
                data_icon->height = bmColour[i].bmHeight;
                data_icon->planes = bmColour[i].bmPlanes;
                data_icon->bpp    = bmColour[i].bmBitsPixel;
                GetBitmapBits(iconinfo[i].hbmColor, cbColourBits[i], &data_icon->buffer[cbMaskBits[i]]);
                DeleteObject(iconinfo[i].hbmColor);
            }
            DeleteObject(iconinfo[i].hbmMask);
            data_icon = (struct notify_data_icon*)&data_icon->buffer[cbMaskBits[i] + cbColourBits[i]];
        }
    }

    data->hWnd   = HandleToLong( nid->hWnd );
    data->uID    = nid->uID;
    data->uFlags = nid->uFlags;
    if (data->uFlags & NIF_MESSAGE)
        data->uCallbackMessage = nid->uCallbackMessage;
    if (data->uFlags & NIF_TIP)
        lstrcpynW( data->szTip, nid->szTip, ARRAY_SIZE(data->szTip));
    if (data->uFlags & NIF_STATE)
    {
        data->dwState     = nid->dwState;
        data->dwStateMask = nid->dwStateMask;
    }
    if (data->uFlags & NIF_INFO)
    {
        lstrcpynW( data->szInfo, nid->szInfo, ARRAY_SIZE(data->szInfo) );
        lstrcpynW( data->szInfoTitle, nid->szInfoTitle, ARRAY_SIZE(data->szInfoTitle));
        data->u.uTimeout  = nid->uTimeout;
        data->dwInfoFlags = nid->dwInfoFlags;
    }
    if (data->uFlags & NIF_GUID)
        data->guidItem = nid->guidItem;
    if (dwMessage == NIM_SETVERSION)
        data->u.uVersion = nid->uVersion;
    /* FIXME: balloon icon */

    cds.lpData = data;
    ret = SendMessageW(tray, WM_COPYDATA, (WPARAM)nid->hWnd, (LPARAM)&cds);
    if (data != &data_buffer) free(data);
    SetLastError(ret ? S_OK : E_FAIL);
    return ret;
}

/*************************************************************************
 * Shell_NotifyIconGetRect		[SHELL32.@]
 */
HRESULT WINAPI Shell_NotifyIconGetRect(const NOTIFYICONIDENTIFIER* identifier, RECT* icon_location)
{
    FIXME("stub (%p) (%p)\n", identifier, icon_location);
    return E_NOTIMPL;
}
