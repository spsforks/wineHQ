/* WinRT Windows.Devices.Enumeration implementation
 *
 * Copyright 2021 Gijs Vermeulen
 * Copyright 2022 Julian Klemann for CodeWeavers
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

#ifndef __WINE_WINDOWS_DEVICES_ENUMERATION_PRIVATE_H
#define __WINE_WINDOWS_DEVICES_ENUMERATION_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Devices_Enumeration
#include "windows.devices.enumeration.h"

#include "wine/list.h"
#include "wine/winrt.h"

extern IActivationFactory *device_access_factory;

HRESULT typed_event_handlers_append( struct list *list, ITypedEventHandler_IInspectable_IInspectable *handler, EventRegistrationToken *token );
HRESULT typed_event_handlers_remove( struct list *list, EventRegistrationToken *token );
HRESULT typed_event_handlers_notify( struct list *list, IInspectable *sender, IInspectable *args );
HRESULT typed_event_handlers_clear( struct list *list );

#endif
