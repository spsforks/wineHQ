/* WinRT Vector<IInspectable> implementation
 *
 * Copyright 2021 Rémi Bernon for CodeWeavers
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

WINE_DEFAULT_DEBUG_CHANNEL(winrt);

struct iterator_inspectable
{
    IIterator_IInspectable IIterator_IInspectable_iface;
    const GUID *iid;
    LONG ref;

    IVectorView_IInspectable *view;
    UINT32 index;
    UINT32 size;
};

static inline struct iterator_inspectable *impl_from_IIterator_IInspectable( IIterator_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct iterator_inspectable, IIterator_IInspectable_iface );
}

static HRESULT WINAPI iterator_inspectable_QueryInterface( IIterator_IInspectable *iface, REFIID iid, void **out )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iid ))
    {
        IInspectable_AddRef( (*out = &impl->IIterator_IInspectable_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_inspectable_AddRef( IIterator_IInspectable *iface )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI iterator_inspectable_Release( IIterator_IInspectable *iface )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        IVectorView_IInspectable_Release( impl->view );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI iterator_inspectable_GetIids( IIterator_IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterator_inspectable_GetRuntimeClassName( IIterator_IInspectable *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterator_inspectable_GetTrustLevel( IIterator_IInspectable *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterator_inspectable_get_Current( IIterator_IInspectable *iface, IInspectable **value )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return IVectorView_IInspectable_GetAt( impl->view, impl->index, value );
}

static HRESULT WINAPI iterator_inspectable_get_HasCurrent( IIterator_IInspectable *iface, BOOL *value )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT WINAPI iterator_inspectable_MoveNext( IIterator_IInspectable *iface, BOOL *value )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->index < impl->size) impl->index++;
    return IIterator_IInspectable_get_HasCurrent( iface, value );
}

static HRESULT WINAPI iterator_inspectable_GetMany( IIterator_IInspectable *iface, UINT32 items_size,
                                                    IInspectable **items, UINT32 *count )
{
    struct iterator_inspectable *impl = impl_from_IIterator_IInspectable( iface );
    TRACE( "iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count );
    return IVectorView_IInspectable_GetMany( impl->view, impl->index, items_size, items, count );
}

static const IIterator_IInspectableVtbl iterator_inspectable_vtbl =
{
    /* IUnknown methods */
    iterator_inspectable_QueryInterface,
    iterator_inspectable_AddRef,
    iterator_inspectable_Release,
    /* IInspectable methods */
    iterator_inspectable_GetIids,
    iterator_inspectable_GetRuntimeClassName,
    iterator_inspectable_GetTrustLevel,
    /* IIterator<IInspectable*> methods */
    iterator_inspectable_get_Current,
    iterator_inspectable_get_HasCurrent,
    iterator_inspectable_MoveNext,
    iterator_inspectable_GetMany,
};

struct vector_view_inspectable
{
    IVectorView_IInspectable IVectorView_IInspectable_iface;
    IIterable_IInspectable IIterable_IInspectable_iface;
    struct vector_iids iids;
    LONG ref;

    UINT32 size;
    IInspectable *elements[];
};

C_ASSERT( sizeof(struct vector_view_inspectable) == offsetof( struct vector_view_inspectable, elements[0] ) );

static inline struct vector_view_inspectable *impl_from_IVectorView_IInspectable( IVectorView_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct vector_view_inspectable, IVectorView_IInspectable_iface );
}

static HRESULT WINAPI vector_view_inspectable_QueryInterface( IVectorView_IInspectable *iface, REFIID iid, void **out )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iids.view ))
    {
        IInspectable_AddRef( (*out = &impl->IVectorView_IInspectable_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, impl->iids.iterable ))
    {
        IInspectable_AddRef( (*out = &impl->IIterable_IInspectable_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vector_view_inspectable_AddRef( IVectorView_IInspectable *iface )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI vector_view_inspectable_Release( IVectorView_IInspectable *iface )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );
    ULONG i, ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        for (i = 0; i < impl->size; ++i) IInspectable_Release( impl->elements[i] );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI vector_view_inspectable_GetIids( IVectorView_IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_view_inspectable_GetRuntimeClassName( IVectorView_IInspectable *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_view_inspectable_GetTrustLevel( IVectorView_IInspectable *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_view_inspectable_GetAt( IVectorView_IInspectable *iface, UINT32 index, IInspectable **value )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    *value = NULL;
    if (index >= impl->size) return E_BOUNDS;

    IInspectable_AddRef( (*value = impl->elements[index]) );
    return S_OK;
}

static HRESULT WINAPI vector_view_inspectable_get_Size( IVectorView_IInspectable *iface, UINT32 *value )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = impl->size;
    return S_OK;
}

static HRESULT WINAPI vector_view_inspectable_IndexOf( IVectorView_IInspectable *iface, IInspectable *element,
                                                       UINT32 *index, BOOLEAN *found )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );
    ULONG i;

    TRACE( "iface %p, element %p, index %p, found %p.\n", iface, element, index, found );

    for (i = 0; i < impl->size; ++i) if (impl->elements[i] == element) break;
    if ((*found = (i < impl->size))) *index = i;
    else *index = 0;

    return S_OK;
}

static HRESULT WINAPI vector_view_inspectable_GetMany( IVectorView_IInspectable *iface, UINT32 start_index,
                                                       UINT32 items_size, IInspectable **items, UINT32 *count )
{
    struct vector_view_inspectable *impl = impl_from_IVectorView_IInspectable( iface );
    UINT32 i;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, count %p.\n",
           iface, start_index, items_size, items, count );

    if (start_index >= impl->size) return E_BOUNDS;

    for (i = start_index; i < impl->size; ++i)
    {
        if (i - start_index >= items_size) break;
        IInspectable_AddRef( (items[i - start_index] = impl->elements[i]) );
    }
    *count = i - start_index;

    return S_OK;
}

static const struct IVectorView_IInspectableVtbl vector_view_inspectable_vtbl =
{
    /* IUnknown methods */
    vector_view_inspectable_QueryInterface,
    vector_view_inspectable_AddRef,
    vector_view_inspectable_Release,
    /* IInspectable methods */
    vector_view_inspectable_GetIids,
    vector_view_inspectable_GetRuntimeClassName,
    vector_view_inspectable_GetTrustLevel,
    /* IVectorView<IInspectable*> methods */
    vector_view_inspectable_GetAt,
    vector_view_inspectable_get_Size,
    vector_view_inspectable_IndexOf,
    vector_view_inspectable_GetMany,
};

DEFINE_IINSPECTABLE_( iterable_view_inspectable, IIterable_IInspectable, struct vector_view_inspectable, view_impl_from_IIterable_IInspectable,
                      IIterable_IInspectable_iface, &impl->IVectorView_IInspectable_iface )

static HRESULT WINAPI iterable_view_inspectable_First( IIterable_IInspectable *iface, IIterator_IInspectable **value )
{
    struct vector_view_inspectable *impl = view_impl_from_IIterable_IInspectable( iface );
    struct iterator_inspectable *iter;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!(iter = calloc( 1, sizeof(struct iterator_inspectable) ))) return E_OUTOFMEMORY;
    iter->IIterator_IInspectable_iface.lpVtbl = &iterator_inspectable_vtbl;
    iter->iid = impl->iids.iterator;
    iter->ref = 1;

    IVectorView_IInspectable_AddRef( (iter->view = &impl->IVectorView_IInspectable_iface) );
    iter->size = impl->size;

    *value = &iter->IIterator_IInspectable_iface;
    return S_OK;
}

static const struct IIterable_IInspectableVtbl iterable_view_inspectable_vtbl =
{
    /* IUnknown methods */
    iterable_view_inspectable_QueryInterface,
    iterable_view_inspectable_AddRef,
    iterable_view_inspectable_Release,
    /* IInspectable methods */
    iterable_view_inspectable_GetIids,
    iterable_view_inspectable_GetRuntimeClassName,
    iterable_view_inspectable_GetTrustLevel,
    /* IIterable<T> methods */
    iterable_view_inspectable_First,
};

struct vector_inspectable
{
    IVector_IInspectable IVector_IInspectable_iface;
    IIterable_IInspectable IIterable_IInspectable_iface;
    struct vector_iids iids;
    LONG ref;

    UINT32 size;
    UINT32 capacity;
    IInspectable **elements;
};

static inline struct vector_inspectable *impl_from_IVector_IInspectable( IVector_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct vector_inspectable, IVector_IInspectable_iface );
}

static HRESULT WINAPI vector_inspectable_QueryInterface( IVector_IInspectable *iface, REFIID iid, void **out )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iids.vector ))
    {
        IInspectable_AddRef( (*out = &impl->IVector_IInspectable_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, impl->iids.iterable ))
    {
        IInspectable_AddRef( (*out = &impl->IIterable_IInspectable_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vector_inspectable_AddRef( IVector_IInspectable *iface )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI vector_inspectable_Release( IVector_IInspectable *iface )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        IVector_IInspectable_Clear( iface );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI vector_inspectable_GetIids( IVector_IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_inspectable_GetRuntimeClassName( IVector_IInspectable *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_inspectable_GetTrustLevel( IVector_IInspectable *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_inspectable_GetAt( IVector_IInspectable *iface, UINT32 index, IInspectable **value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    *value = NULL;
    if (index >= impl->size) return E_BOUNDS;

    IInspectable_AddRef( (*value = impl->elements[index]) );
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_get_Size( IVector_IInspectable *iface, UINT32 *value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->size;
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_GetView( IVector_IInspectable *iface, IVectorView_IInspectable **value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    struct vector_view_inspectable *view;
    ULONG i;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!(view = calloc( 1, offsetof( struct vector_view_inspectable, elements[impl->size] ) ))) return E_OUTOFMEMORY;
    view->IVectorView_IInspectable_iface.lpVtbl = &vector_view_inspectable_vtbl;
    view->IIterable_IInspectable_iface.lpVtbl = &iterable_view_inspectable_vtbl;
    view->iids = impl->iids;
    view->ref = 1;

    for (i = 0; i < impl->size; ++i) IInspectable_AddRef( (view->elements[view->size++] = impl->elements[i]) );

    *value = &view->IVectorView_IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_IndexOf( IVector_IInspectable *iface, IInspectable *element,
                                                  UINT32 *index, BOOLEAN *found )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    ULONG i;

    TRACE( "iface %p, element %p, index %p, found %p.\n", iface, element, index, found );

    for (i = 0; i < impl->size; ++i) if (impl->elements[i] == element) break;
    if ((*found = (i < impl->size))) *index = i;
    else *index = 0;

    return S_OK;
}

static HRESULT WINAPI vector_inspectable_SetAt( IVector_IInspectable *iface, UINT32 index, IInspectable *value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (index >= impl->size) return E_BOUNDS;
    IInspectable_Release( impl->elements[index] );
    IInspectable_AddRef( (impl->elements[index] = value) );
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_InsertAt( IVector_IInspectable *iface, UINT32 index, IInspectable *value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    IInspectable **tmp = impl->elements;

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (impl->size == impl->capacity)
    {
        impl->capacity = max( 32, impl->capacity * 3 / 2 );
        if (!(impl->elements = realloc( impl->elements, impl->capacity * sizeof(*impl->elements) )))
        {
            impl->elements = tmp;
            return E_OUTOFMEMORY;
        }
    }

    memmove( impl->elements + index + 1, impl->elements + index, (impl->size++ - index) * sizeof(*impl->elements) );
    IInspectable_AddRef( (impl->elements[index] = value) );
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_RemoveAt( IVector_IInspectable *iface, UINT32 index )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p, index %u.\n", iface, index );

    if (index >= impl->size) return E_BOUNDS;
    IInspectable_Release( impl->elements[index] );
    memmove( impl->elements + index, impl->elements + index + 1, (--impl->size - index) * sizeof(*impl->elements) );
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_Append( IVector_IInspectable *iface, IInspectable *value )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    return IVector_IInspectable_InsertAt( iface, impl->size, value );
}

static HRESULT WINAPI vector_inspectable_RemoveAtEnd( IVector_IInspectable *iface )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p.\n", iface );

    if (impl->size) IInspectable_Release( impl->elements[--impl->size] );
    return S_OK;
}

static HRESULT WINAPI vector_inspectable_Clear( IVector_IInspectable *iface )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );

    TRACE( "iface %p.\n", iface );

    while (impl->size) IVector_IInspectable_RemoveAtEnd( iface );
    free( impl->elements );
    impl->capacity = 0;
    impl->elements = NULL;

    return S_OK;
}

static HRESULT WINAPI vector_inspectable_GetMany( IVector_IInspectable *iface, UINT32 start_index,
                                                  UINT32 items_size, IInspectable **items, UINT32 *count )
{
    struct vector_inspectable *impl = impl_from_IVector_IInspectable( iface );
    UINT32 i;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, count %p.\n",
           iface, start_index, items_size, items, count );

    if (start_index >= impl->size) return E_BOUNDS;

    for (i = start_index; i < impl->size; ++i)
    {
        if (i - start_index >= items_size) break;
        IInspectable_AddRef( (items[i - start_index] = impl->elements[i]) );
    }
    *count = i - start_index;

    return S_OK;
}

static HRESULT WINAPI vector_inspectable_ReplaceAll( IVector_IInspectable *iface, UINT32 count, IInspectable **items )
{
    HRESULT hr;
    ULONG i;

    TRACE( "iface %p, count %u, items %p.\n", iface, count, items );

    hr = IVector_IInspectable_Clear( iface );
    for (i = 0; i < count && SUCCEEDED(hr); ++i) hr = IVector_IInspectable_Append( iface, items[i] );
    return hr;
}

static const struct IVector_IInspectableVtbl vector_inspectable_vtbl =
{
    /* IUnknown methods */
    vector_inspectable_QueryInterface,
    vector_inspectable_AddRef,
    vector_inspectable_Release,
    /* IInspectable methods */
    vector_inspectable_GetIids,
    vector_inspectable_GetRuntimeClassName,
    vector_inspectable_GetTrustLevel,
    /* IVector<IInspectable*> methods */
    vector_inspectable_GetAt,
    vector_inspectable_get_Size,
    vector_inspectable_GetView,
    vector_inspectable_IndexOf,
    vector_inspectable_SetAt,
    vector_inspectable_InsertAt,
    vector_inspectable_RemoveAt,
    vector_inspectable_Append,
    vector_inspectable_RemoveAtEnd,
    vector_inspectable_Clear,
    vector_inspectable_GetMany,
    vector_inspectable_ReplaceAll,
};

DEFINE_IINSPECTABLE( iterable_inspectable, IIterable_IInspectable, struct vector_inspectable, IVector_IInspectable_iface )

static HRESULT WINAPI iterable_inspectable_First( IIterable_IInspectable *iface, IIterator_IInspectable **value )
{
    struct vector_inspectable *impl = impl_from_IIterable_IInspectable( iface );
    IIterable_IInspectable *iterable;
    IVectorView_IInspectable *view;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (FAILED(hr = IVector_IInspectable_GetView( &impl->IVector_IInspectable_iface, &view ))) return hr;

    hr = IVectorView_IInspectable_QueryInterface( view, impl->iids.iterable, (void **)&iterable );
    IVectorView_IInspectable_Release( view );
    if (FAILED(hr)) return hr;

    hr = IIterable_IInspectable_First( iterable, value );
    IIterable_IInspectable_Release( iterable );
    return hr;
}

static const struct IIterable_IInspectableVtbl iterable_inspectable_vtbl =
{
    /* IUnknown methods */
    iterable_inspectable_QueryInterface,
    iterable_inspectable_AddRef,
    iterable_inspectable_Release,
    /* IInspectable methods */
    iterable_inspectable_GetIids,
    iterable_inspectable_GetRuntimeClassName,
    iterable_inspectable_GetTrustLevel,
    /* IIterable<T> methods */
    iterable_inspectable_First,
};

HRESULT vector_inspectable_create( const struct vector_iids *iids, void **out )
{
    struct vector_inspectable *impl;

    TRACE( "iid %s, out %p.\n", debugstr_guid( iids->vector ), out );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVector_IInspectable_iface.lpVtbl = &vector_inspectable_vtbl;
    impl->IIterable_IInspectable_iface.lpVtbl = &iterable_inspectable_vtbl;
    impl->iids = *iids;
    impl->ref = 1;

    *out = &impl->IVector_IInspectable_iface;
    TRACE( "created %p\n", *out );
    return S_OK;
}
