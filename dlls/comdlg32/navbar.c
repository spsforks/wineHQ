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
#include "filedlgbrowser.h"
#include "shlwapi.h"

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(commdlg);

/* private control ids */
#define IDC_NAVBACK 201
#define IDC_NAVFORWARD 202
#define IDC_NAVUP 203
#define IDC_NAVCRUMB 204

typedef struct {
    HWND parent_hwnd;
    HWND container_hwnd;
    INT container_h;
    INT dpi_x;

    HIMAGELIST icons;
    HWND tooltip;

    HWND back_btn_hwnd;
    HWND fwd_btn_hwnd;
    HWND up_btn_hwnd;

    struct list crumbs;
    INT crumbs_total_n;
    INT crumbs_visible_n;
} NAVBAR_INFO;

struct crumb {
    struct list entry;
    ITEMIDLIST *pidl;
    WCHAR *display_name;
    HWND hwnd;
    INT full_w;
    INT current_w;
    INT x;
};

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

static void NAVBAR_CalcLayout(NAVBAR_INFO *info)
{
    RECT container_rc,
         button_rc;
    struct crumb *crumb;
    INT container_w = 0,
        gap = MulDiv(1, info->dpi_x, USER_DEFAULT_SCREEN_DPI),
        buttons_w = 0,
        max_crumbs_w = 0,
        w = 0,
        crumbs_visible_n = 0,
        prev_x = 0;

    if (!GetClientRect(info->container_hwnd, &container_rc) ||
        !(container_w = container_rc.right - container_rc.left) ||
        !GetClientRect(info->up_btn_hwnd, &button_rc))
        return;

    buttons_w = (button_rc.right - button_rc.left + gap) * 3;
    max_crumbs_w = container_w - buttons_w;
    if (max_crumbs_w < 0)
        return;

    LIST_FOR_EACH_ENTRY_REV(crumb, &info->crumbs, struct crumb, entry)
    {
        INT next_w = w + crumb->full_w;
        crumb->current_w = crumb->full_w;

        if (next_w > max_crumbs_w)
        {
            if (crumbs_visible_n == 0)
            {
                /* this last crumb doesn't fully fit, take all available free space */
                crumb->current_w = max_crumbs_w;
                w = max_crumbs_w;
                crumbs_visible_n += 1;
                prev_x = buttons_w + crumb->current_w;

                /* try to also fit the next crumb */
                continue;
            }
            else if (crumbs_visible_n == 1)
            {
                struct crumb *last_crumb = (struct crumb *)list_tail(&info->crumbs);
                INT crumb_w = min(MulDiv(56, info->dpi_x, USER_DEFAULT_SCREEN_DPI), crumb->full_w);

                if (w + crumb_w <= max_crumbs_w)
                    /* last crumb fully fits, let this crumb take the remaining free space */
                    crumb->current_w = max_crumbs_w - w;
                else
                {
                    /* last crumb doesn't fully fit, let this crumb take the minimum amount of space, and last crumb take the remaining space */
                    crumb->current_w = crumb_w;
                    last_crumb->current_w = max(0, max_crumbs_w - crumb->current_w);
                }

                crumbs_visible_n += 1;
                prev_x = buttons_w + crumb->current_w + last_crumb->current_w;
            }

            break;
        }

        w = next_w;
        crumbs_visible_n += 1;
        prev_x = buttons_w + w;
    }

    info->crumbs_visible_n = crumbs_visible_n;

    LIST_FOR_EACH_ENTRY_REV(crumb, &info->crumbs, struct crumb, entry)
    {
        if (crumbs_visible_n > 0)
        {
            LONG_PTR style = WS_CHILD | WS_VISIBLE;

            /* if label doesn't fully fit, align it to the left */
            if (crumb->current_w < crumb->full_w)
                style |= BS_LEFT;
            else
                style |= BS_CENTER;

            SetWindowLongPtrW(crumb->hwnd, GWL_STYLE, style);

            crumb->x = prev_x - crumb->current_w;
            prev_x = crumb->x;
        }
        else
            break;

        crumbs_visible_n -= 1;
    }
}

static HDWP NAVBAR_DoLayout(NAVBAR_INFO *info, HDWP hdwp)
{
    struct crumb *crumb;
    INT can_fit_n = info->crumbs_visible_n;

    LIST_FOR_EACH_ENTRY_REV(crumb, &info->crumbs, struct crumb, entry)
    {
        UINT flags = 0;

        if (can_fit_n > 0)
            flags |= SWP_SHOWWINDOW | SWP_NOCOPYBITS;
        else
            flags |= SWP_HIDEWINDOW;

        hdwp = DeferWindowPos(hdwp, crumb->hwnd, HWND_TOP,
                              crumb->x, 0,
                              crumb->current_w, info->container_h,
                              flags);

        can_fit_n -= 1;
    }

    return hdwp;
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
    list_init(&info->crumbs);
    info->parent_hwnd = GetParent(hwnd);
    info->container_hwnd = hwnd;
    info->container_h = cs->cy;

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

    x += cs->cy + gap;
    info->up_btn_hwnd = CreateWindowExW(0, WC_BUTTONW, NULL,
                                        WS_CHILD | WS_VISIBLE | BS_ICON | BS_BITMAP,
                                        x, 0, cs->cy, cs->cy,
                                        hwnd, (HMENU)IDC_NAVUP, COMDLG32_hInstance, NULL);
    SendMessageW(info->up_btn_hwnd, WM_SETFONT, (WPARAM)gui_font, FALSE);
    set_icon(info->icons, ILI_UP, info->up_btn_hwnd);
    set_title_and_add_tooltip(info, info->up_btn_hwnd, IDS_UPFOLDER);

    SetWindowLongPtrW(hwnd, 0, (DWORD_PTR)info);

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT NAVBAR_Destroy(HWND hwnd, NAVBAR_INFO *info, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct crumb *crumb1, *crumb2;

    LIST_FOR_EACH_ENTRY_SAFE(crumb1, crumb2, &info->crumbs, struct crumb, entry)
    {
        ILFree(crumb1->pidl);
        CoTaskMemFree(crumb1->display_name);
        list_remove(&crumb1->entry);
        HeapFree(GetProcessHeap(), 0, crumb1);
    }

    SetWindowLongPtrW(hwnd, 0, 0);
    ImageList_Destroy(info->icons);

    HeapFree(GetProcessHeap(), 0, info);

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT NAVBAR_Size(HWND hwnd, NAVBAR_INFO *info, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HDWP hdwp;

    NAVBAR_CalcLayout(info);

    hdwp = BeginDeferWindowPos(info->crumbs_total_n);
    hdwp = NAVBAR_DoLayout(info, hdwp);
    EndDeferWindowPos(hdwp);

    return TRUE;
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
        case IDC_NAVUP:
            SendMessageW(info->parent_hwnd, NBN_NAVUP, 0, 0);
            break;
        case IDC_NAVCRUMB:
        {
            HWND crumb = (HWND)lparam;
            ITEMIDLIST *pidl = (ITEMIDLIST*)GetWindowLongPtrW(crumb, GWLP_USERDATA);
            SendMessageW(info->parent_hwnd, NBN_NAVPIDL, 0, (LPARAM)pidl);
            break;
        }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT NAVBAR_SetPIDL(HWND hwnd, NAVBAR_INFO *info, UINT msg, WPARAM wparam, LPARAM lparam)
{
    ITEMIDLIST *pidl = (ITEMIDLIST *)lparam;
    IShellFolder *desktop;
    HGDIOBJ gui_font = GetStockObject(DEFAULT_GUI_FONT);
    HRESULT hr;
    INT padding = MulDiv(10, info->dpi_x, USER_DEFAULT_SCREEN_DPI);
    struct list new_crumbs;
    struct crumb *crumb1, *crumb2;
    HDWP hdwp;

    TRACE("info %p pidl %p\n", info, pidl);

    hr = SHGetDesktopFolder(&desktop);
    if (FAILED(hr))
    {
        ERR("failed to get desktop folder\n");
        goto exit;
    }

    list_init(&new_crumbs);
    info->crumbs_total_n = 0;
    pidl = ILClone(pidl);
    do
    {
        STRRET strret;
        SIZE full_size;

        crumb1 = HeapAlloc(GetProcessHeap(), 0, sizeof(*crumb1));

        crumb1->pidl = ILClone(pidl);
        IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORADDRESSBAR, &strret);
        StrRetToStrW(&strret, pidl, &crumb1->display_name);

        crumb1->hwnd = CreateWindowExW(0, WC_BUTTONW, crumb1->display_name, WS_CHILD,
                                       0, 0, 0, 0,
                                       info->container_hwnd, (HMENU)IDC_NAVCRUMB, COMDLG32_hInstance, NULL);
        SendMessageW(crumb1->hwnd, WM_SETFONT, (LPARAM)gui_font, FALSE);
        SendMessageW(crumb1->hwnd, BCM_GETIDEALSIZE, 0, (LPARAM)&full_size);
        SetWindowLongPtrW(crumb1->hwnd, GWLP_USERDATA, (LPARAM)crumb1->pidl);

        crumb1->full_w = full_size.cx + padding;
        crumb1->current_w = crumb1->full_w;

        /* PIDL is iterated from right-to-left, prepend the crumb to store them in left-to-right order */
        list_add_head(&new_crumbs, &crumb1->entry);
        info->crumbs_total_n += 1;
    }
    while (ILRemoveLastID(pidl));
    ILFree(pidl);
    IShellFolder_Release(desktop);

    /* reuse existing crumbs */
    crumb1 = LIST_ENTRY((&info->crumbs)->next, struct crumb, entry);
    crumb2 = LIST_ENTRY((&new_crumbs)->next, struct crumb, entry);
    for (;;)
    {
        if (&crumb1->entry == &info->crumbs ||
            &crumb2->entry == &new_crumbs ||
            !ILIsEqual(crumb2->pidl, crumb1->pidl))
            break;

        DestroyWindow(crumb2->hwnd);
        ILFree(crumb2->pidl);
        CoTaskMemFree(crumb2->display_name);
        crumb2->pidl = crumb1->pidl;
        crumb2->display_name = crumb1->display_name;
        crumb2->hwnd = crumb1->hwnd;

        crumb1 = LIST_ENTRY(crumb1->entry.next, struct crumb, entry);
        crumb2 = LIST_ENTRY(crumb2->entry.next, struct crumb, entry);
    }

    /* cleanup unused existing crumbs */
    for (;;)
    {
        if (&crumb1->entry == &info->crumbs)
            break;

        DestroyWindow(crumb1->hwnd);
        ILFree(crumb1->pidl);
        CoTaskMemFree(crumb1->display_name);

        crumb1 = LIST_ENTRY(crumb1->entry.next, struct crumb, entry);
    }

    LIST_FOR_EACH_ENTRY_SAFE(crumb1, crumb2, &info->crumbs, struct crumb, entry)
    {
        list_remove(&crumb1->entry);
        HeapFree(GetProcessHeap(), 0, crumb1);
    }

    new_crumbs.next->prev = &info->crumbs;
    new_crumbs.prev->next = &info->crumbs;
    info->crumbs = new_crumbs;

    NAVBAR_CalcLayout(info);

    hdwp = BeginDeferWindowPos(info->crumbs_total_n);
    hdwp = NAVBAR_DoLayout(info, hdwp);
    EndDeferWindowPos(hdwp);

exit:
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
        case WM_SIZE: return NAVBAR_Size(hwnd, info, msg, wparam, lparam);
        case WM_COMMAND: return NAVBAR_Command(hwnd, info, msg, wparam, lparam);
        case NBM_SETPIDL: return NAVBAR_SetPIDL(hwnd, info, msg, wparam, lparam);
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
