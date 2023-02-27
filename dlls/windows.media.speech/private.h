/* WinRT Windows.Media.Speech private header
 *
 * Copyright 2022 Bernhard Kölbl
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

#ifndef __WINE_WINDOWS_MEDIA_SPEECH_PRIVATE_H
#define __WINE_WINDOWS_MEDIA_SPEECH_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "corerror.h"
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Globalization
#include "windows.globalization.h"
#define WIDL_using_Windows_Media_SpeechSynthesis
#include "windows.media.speechsynthesis.h"
#define WIDL_using_Windows_Media_SpeechRecognition
#include "windows.media.speechrecognition.h"

#include "wine/list.h"
#include "wine/winrt.h"

/*
 *
 * Windows.Media.SpeechRecognition
 *
 */

extern IActivationFactory *listconstraint_factory;
extern IActivationFactory *recognizer_factory;

/*
 *
 * Windows.Media.SpeechSynthesis
 *
 */

extern IActivationFactory *synthesizer_factory;



struct vector_iids
{
    const GUID *iterable;
    const GUID *iterator;
    const GUID *vector;
    const GUID *view;
};

typedef HRESULT (*async_action_callback)( IInspectable *invoker );
typedef HRESULT (*async_operation_inspectable_callback)( IInspectable *invoker, IInspectable **result );

HRESULT async_action_create( IInspectable *invoker, async_action_callback callback, IAsyncAction **out );
HRESULT async_operation_inspectable_create( const GUID *iid, IInspectable *invoker, async_operation_inspectable_callback callback,
                                            IAsyncOperation_IInspectable **out );

HRESULT typed_event_handlers_append( struct list *list, ITypedEventHandler_IInspectable_IInspectable *handler, EventRegistrationToken *token );
HRESULT typed_event_handlers_remove( struct list *list, EventRegistrationToken *token );
HRESULT typed_event_handlers_notify( struct list *list, IInspectable *sender, IInspectable *args );
HRESULT typed_event_handlers_clear( struct list* list );

HRESULT vector_hstring_create( IVector_HSTRING **out );
HRESULT vector_hstring_create_copy( IIterable_HSTRING *iterable, IVector_HSTRING **out );
HRESULT vector_inspectable_create( const struct vector_iids *iids, IVector_IInspectable **out );

#endif
