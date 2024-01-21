/*
 * IDirect3DDevice9On12 implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#include "d3d9_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d9);

static inline struct d3d9on12 *impl_from_IDirect3DDevice9On12( IDirect3DDevice9On12 *iface )
{
    return CONTAINING_RECORD( iface, struct d3d9on12, IDirect3DDevice9On12_iface );
}

static HRESULT WINAPI d3d9on12_QueryInterface( IDirect3DDevice9On12 *iface, REFIID iid, void **out )
{
    struct d3d9on12 *d3d9on12 = impl_from_IDirect3DDevice9On12(iface);

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IDirect3DDevice9On12 ))
    {
        IDirect3DDevice9On12_AddRef( &d3d9on12->IDirect3DDevice9On12_iface );
        *out = &d3d9on12->IDirect3DDevice9On12_iface;
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d9on12_AddRef( IDirect3DDevice9On12 *iface )
{
    struct d3d9on12 *d3d9on12 = impl_from_IDirect3DDevice9On12(iface);
    ULONG refcount = InterlockedIncrement(&d3d9on12->refcount);
    TRACE( "%p increasing refcount to %lu.\n", iface, refcount );
    return refcount;
}

static ULONG WINAPI d3d9on12_Release( IDirect3DDevice9On12 *iface )
{
    struct d3d9on12 *d3d9on12 = impl_from_IDirect3DDevice9On12(iface);
    ULONG refcount = InterlockedDecrement(&d3d9on12->refcount);

    TRACE( "%p decreasing refcount to %lu.\n", iface, refcount );

    if (!refcount)
    {
        if (d3d9on12->override_list)
        {
            if (d3d9on12->override_list->pD3D12Device)
                ID3D12Device_Release( (ID3D12Device *)d3d9on12->override_list->pD3D12Device );

            for ( int i = 0; i < d3d9on12->override_list->NumQueues; i++ )
            {
                if (d3d9on12->override_list->ppD3D12Queues[i])
                    ID3D12CommandQueue_Release( (ID3D12CommandQueue *)d3d9on12->override_list->ppD3D12Queues[i] );
            }

            free( d3d9on12->override_list );
        }

        free( d3d9on12 );
    }

    return refcount;
}

static HRESULT WINAPI d3d9on12_GetD3D12Device( IDirect3DDevice9On12 *iface, REFIID iid, void **out )
{
    FIXME( "iface %p, iid %s, out %p stub!\n", iface, debugstr_guid(iid), out );

    if (!out) return E_INVALIDARG;

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI d3d9on12_UnwrapUnderlyingResource( IDirect3DDevice9On12 *iface, IDirect3DResource9 *resource, ID3D12CommandQueue *queue, REFIID iid, void **out )
{
    FIXME( "iface %p, resource %p, queue %p, iid %s, out %p stub!\n", iface, resource, queue, debugstr_guid(iid), out );
    return E_NOTIMPL;
}

static HRESULT WINAPI d3d9on12_ReturnUnderlyingResource( IDirect3DDevice9On12 *iface, IDirect3DResource9 *resource, UINT num_sync, UINT64 *signal_values, ID3D12Fence **fences )
{
    FIXME( "iface %p, resource %p, num_sync %#x, signal_values %p, fences %p stub!\n", iface, resource, num_sync, signal_values, fences );
    return E_NOTIMPL;
}

static const struct IDirect3DDevice9On12Vtbl d3d9on12_vtbl =
{
    /* IUnknown */
    d3d9on12_QueryInterface,
    d3d9on12_AddRef,
    d3d9on12_Release,
    /* IDirect3DDevice9On12 */
    d3d9on12_GetD3D12Device,
    d3d9on12_UnwrapUnderlyingResource,
    d3d9on12_ReturnUnderlyingResource,
};

HRESULT d3d9on12_init( struct d3d9on12 **d3d9on12, D3D9ON12_ARGS *override_list, UINT override_entries )
{
    struct d3d9on12 *object;

    if (!override_entries || !override_list || !override_list->Enable9On12)
    {
        *d3d9on12 = NULL;
        return E_INVALIDARG;
    }

    if (!(object = calloc( 1, sizeof(*object) )))
    {
        free( object );
        *d3d9on12 = NULL;
        return E_OUTOFMEMORY;
    }

    FIXME( "ignoring override_list %p\n", override_list );

    object->IDirect3DDevice9On12_iface.lpVtbl = &d3d9on12_vtbl;
    object->refcount = 0;
    object->override_list = NULL;
    object->override_entries = override_entries;

    *d3d9on12 = object;
    return S_OK;
}
