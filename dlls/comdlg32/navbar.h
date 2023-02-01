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

#ifndef _WINE_NAVBAR
#define _WINE_NAVBAR

#include "windows.h"

#define WC_NAVBARW L"NavBar"

/* navbar messages */
#define NBM_SETPIDL WM_USER + 101

/* navbar notifications */
#define NBN_NAVBACK WM_USER + 102
#define NBN_NAVFORWARD WM_USER + 103
#define NBN_NAVUP WM_USER + 104
#define NBN_NAVPIDL WM_USER + 105

/* strings */
#define IDS_BACK 2000
#define IDS_FORWARD 2001

/* bitmaps */
#define IDB_NAVBAR 3000
/* icons in IDB_NAVBAR */
#define ILI_BACK 0
#define ILI_FORWARD 1
#define ILI_UP 2
#define ILI_REFRESH 3

ATOM NAVBAR_Register(void);
BOOL NAVBAR_Unregister(void);

#endif /* _WINE_NAVBAR */
