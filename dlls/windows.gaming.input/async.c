/* WinRT Windows.Gaming.Input implementation
 *
 * Copyright 2022 Bernhard Kölbl for CodeWeavers
 * Copyright 2022 Rémi Bernon for CodeWeavers
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

#include "private.h"
#include "provider.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(input);

struct async_result
{
    IAsyncOperation_ForceFeedbackLoadEffectResult IAsyncOperation_ForceFeedbackLoadEffectResult_iface;
    IWineAsyncInfoImpl *IWineAsyncInfoImpl_inner;
    LONG ref;
};

static inline struct async_result *impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( IAsyncOperation_ForceFeedbackLoadEffectResult *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, IAsyncOperation_ForceFeedbackLoadEffectResult_iface );
}

static HRESULT WINAPI async_result_QueryInterface( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, REFIID iid, void **out )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_ForceFeedbackLoadEffectResult ))
    {
        IInspectable_AddRef( (*out = &impl->IAsyncOperation_ForceFeedbackLoadEffectResult_iface) );
        return S_OK;
    }

    return IWineAsyncInfoImpl_QueryInterface( impl->IWineAsyncInfoImpl_inner, iid, out );
}

static ULONG WINAPI async_result_AddRef( IAsyncOperation_ForceFeedbackLoadEffectResult *iface )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI async_result_Release( IAsyncOperation_ForceFeedbackLoadEffectResult *iface )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        /* guard against re-entry if inner releases an outer iface */
        InterlockedIncrement( &impl->ref );
        IWineAsyncInfoImpl_Release( impl->IWineAsyncInfoImpl_inner );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI async_result_GetIids( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI async_result_GetRuntimeClassName( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.Gaming.Input.ForceFeedback.ForceFeedbackLoadEffectResult>",
                                ARRAY_SIZE(L"Windows.Foundation.IAsyncOperation`1<Windows.Gaming.Input.ForceFeedback.ForceFeedbackLoadEffectResult>"),
                                class_name );
}

static HRESULT WINAPI async_result_GetTrustLevel( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI async_result_put_Completed( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, IAsyncOperationCompletedHandler_ForceFeedbackLoadEffectResult *handler )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );
    TRACE( "iface %p, handler %p.\n", iface, handler );
    return IWineAsyncInfoImpl_put_Completed( impl->IWineAsyncInfoImpl_inner, (IWineAsyncOperationCompletedHandler *)handler );
}

static HRESULT WINAPI async_result_get_Completed( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, IAsyncOperationCompletedHandler_ForceFeedbackLoadEffectResult **handler )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );
    TRACE( "iface %p, handler %p.\n", iface, handler );
    return IWineAsyncInfoImpl_get_Completed( impl->IWineAsyncInfoImpl_inner, (IWineAsyncOperationCompletedHandler **)handler );
}

static HRESULT WINAPI async_result_GetResults( IAsyncOperation_ForceFeedbackLoadEffectResult *iface, ForceFeedbackLoadEffectResult *results )
{
    struct async_result *impl = impl_from_IAsyncOperation_ForceFeedbackLoadEffectResult( iface );
    PROPVARIANT result = {.vt = VT_UI4};
    HRESULT hr;

    TRACE( "iface %p, results %p.\n", iface, results );

    hr = IWineAsyncInfoImpl_get_Result( impl->IWineAsyncInfoImpl_inner, &result );

    *results = result.ulVal;
    PropVariantClear( &result );
    return hr;
}

static const struct IAsyncOperation_ForceFeedbackLoadEffectResultVtbl async_result_vtbl =
{
    /* IUnknown methods */
    async_result_QueryInterface,
    async_result_AddRef,
    async_result_Release,
    /* IInspectable methods */
    async_result_GetIids,
    async_result_GetRuntimeClassName,
    async_result_GetTrustLevel,
    /* IAsyncOperation<ForceFeedbackLoadEffectResult> */
    async_result_put_Completed,
    async_result_get_Completed,
    async_result_GetResults,
};

HRESULT async_operation_effect_result_create( IUnknown *invoker, IUnknown *param, async_callback callback,
                                              IAsyncOperation_ForceFeedbackLoadEffectResult **out )
{
    struct async_result *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_ForceFeedbackLoadEffectResult_iface.lpVtbl = &async_result_vtbl;
    impl->ref = 1;

    if (FAILED(hr = async_info_create( invoker, param, callback, (IInspectable *)&impl->IAsyncOperation_ForceFeedbackLoadEffectResult_iface, &impl->IWineAsyncInfoImpl_inner )) ||
        FAILED(hr = IWineAsyncInfoImpl_Start( impl->IWineAsyncInfoImpl_inner )))
    {
        if (impl->IWineAsyncInfoImpl_inner) IWineAsyncInfoImpl_Release( impl->IWineAsyncInfoImpl_inner );
        free( impl );
        return hr;
    }

    *out = &impl->IAsyncOperation_ForceFeedbackLoadEffectResult_iface;
    TRACE( "created IAsyncOperation_ForceFeedbackLoadEffectResult %p\n", *out );
    return S_OK;
}
