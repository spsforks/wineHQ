/*
 * Navigation bar control
 *
 * Copyright 2022 Vladislav Timonin
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

#define COBJMACROS

#include "navbar.h"
#include "commdlg.h"
#include "cdlg.h"

/* private control ids */
#define IDC_NAVBACK 201
#define IDC_NAVFORWARD 202

typedef struct {
    HWND parent_hwnd;
    HWND container_hwnd;
    INT dpi_x;

    HIMAGELIST icons;
    HWND tooltip;

    HWND back_btn_hwnd;
    HWND fwd_btn_hwnd;
} NAVBAR_INFO;

static void set_icon(HIMAGELIST icons, INT icon_id, HWND window)
{
    HICON icon;

    icon = ImageList_GetIcon(icons, icon_id, ILD_NORMAL);
    SendMessageW(window, BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)icon);
    DestroyIcon(icon);
}

static void set_title_and_add_tooltip(NAVBAR_INFO *info, HWND window, UINT string_id)
{
    WCHAR buffer[128] = {0};
    TOOLINFOW toolinfo = {0};

    LoadStringW(COMDLG32_hInstance, string_id, buffer, ARRAY_SIZE(buffer));

    SetWindowTextW(window, buffer);

    toolinfo.cbSize = sizeof(toolinfo);
    toolinfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolinfo.hwnd = info->container_hwnd;
    toolinfo.lpszText = buffer;
    toolinfo.uId = (UINT_PTR)window;

    SendMessageW(info->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&toolinfo);
}

static LRESULT NAVBAR_Create(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    CREATESTRUCTW *cs = (CREATESTRUCTW*)lparam;
    NAVBAR_INFO *info;
    HDC hdc;
    INT x;
    INT gap;
    HGDIOBJ gui_font = GetStockObject(DEFAULT_GUI_FONT);

    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(NAVBAR_INFO));
    info->parent_hwnd = GetParent(hwnd);
    info->container_hwnd = hwnd;

    hdc = GetDC(0);
    info->dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    gap = MulDiv(1, info->dpi_x, USER_DEFAULT_SCREEN_DPI);
    ReleaseDC(0, hdc);

    info->icons = ImageList_LoadImageW(COMDLG32_hInstance, MAKEINTRESOURCEW(IDB_NAVBAR), 24, 0,
                                       CLR_NONE, IMAGE_BITMAP, LR_CREATEDIBSECTION);

    info->tooltip = CreateWindowW(TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  hwnd, 0, COMDLG32_hInstance, NULL);

    x = 0;
    info->back_btn_hwnd = CreateWindowExW(0, WC_BUTTONW, NULL,
                                          WS_CHILD | WS_VISIBLE | BS_ICON | BS_BITMAP,
                                          x, 0, cs->cy, cs->cy,
                                          hwnd, (HMENU)IDC_NAVBACK, COMDLG32_hInstance, NULL);
    SendMessageW(info->back_btn_hwnd, WM_SETFONT, (WPARAM)gui_font, FALSE);
    set_icon(info->icons, ILI_BACK, info->back_btn_hwnd);
    set_title_and_add_tooltip(info, info->back_btn_hwnd, IDS_BACK);

    x += cs->cy + gap;
    info->fwd_btn_hwnd = CreateWindowExW(0, WC_BUTTONW, NULL,
                                         WS_CHILD | WS_VISIBLE | BS_ICON | BS_BITMAP,
                                         x, 0, cs->cy, cs->cy,
                                         hwnd, (HMENU)IDC_NAVFORWARD, COMDLG32_hInstance, NULL);
    SendMessageW(info->fwd_btn_hwnd, WM_SETFONT, (WPARAM)gui_font, FALSE);
    set_icon(info->icons, ILI_FORWARD, info->fwd_btn_hwnd);
    set_title_and_add_tooltip(info, info->fwd_btn_hwnd, IDS_FORWARD);

    SetWindowLongPtrW(hwnd, 0, (DWORD_PTR)info);

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT NAVBAR_Destroy(HWND hwnd, NAVBAR_INFO *info, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SetWindowLongPtrW(hwnd, 0, 0);
    ImageList_Destroy(info->icons);

    HeapFree(GetProcessHeap(), 0, info);

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT NAVBAR_Command(HWND hwnd, NAVBAR_INFO *info, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (LOWORD(wparam))
    {
        case IDC_NAVBACK:
            SendMessageW(info->parent_hwnd, NBN_NAVBACK, 0, 0);
            break;
        case IDC_NAVFORWARD:
            SendMessageW(info->parent_hwnd, NBN_NAVFORWARD, 0, 0);
            break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK NAVBAR_WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    NAVBAR_INFO *info = (NAVBAR_INFO *)GetWindowLongPtrW(hwnd, 0);

    if (!info && (msg != WM_NCCREATE))
        goto exit;

    switch (msg)
    {
        case WM_NCCREATE: return NAVBAR_Create(hwnd, msg, wparam, lparam);
        case WM_DESTROY: return NAVBAR_Destroy(hwnd, info, msg, wparam, lparam);
        case WM_COMMAND: return NAVBAR_Command(hwnd, info, msg, wparam, lparam);
    }

exit:
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

ATOM NAVBAR_Register(void)
{
    WNDCLASSW wndClass;

    ZeroMemory(&wndClass, sizeof(WNDCLASSW));
    wndClass.style = CS_HREDRAW;
    wndClass.lpfnWndProc = NAVBAR_WindowProc;
    wndClass.cbWndExtra = sizeof(NAVBAR_INFO *);
    wndClass.hInstance = COMDLG32_hInstance;
    wndClass.hCursor = LoadCursorW(0, (LPWSTR)IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)(COLOR_BTNSHADOW);
    wndClass.lpszClassName = WC_NAVBARW;

    return RegisterClassW(&wndClass);
}

BOOL NAVBAR_Unregister(void)
{
    return UnregisterClassW(WC_NAVBARW, COMDLG32_hInstance);
}
