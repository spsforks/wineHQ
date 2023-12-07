/*
 * PropVariant Tests
 *
 * Copyright 2023 Fabian Maurer
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

#include "windows.h"
#include "wtypes.h"
#include "ddeml.h"

#include "wine/test.h"

struct _PMemoryAllocator_vtable {
    void *Allocate; /* virtual void* Allocate(ULONG cbSize); */
    void *Free; /* virtual void Free(void *pv); */
};

typedef struct _PMemoryAllocator {
    struct _PMemoryAllocator_vtable *vt;
} PMemoryAllocator;

static void * WINAPI PMemoryAllocator_Allocate(PMemoryAllocator *_this, ULONG cbSize)
{
    return CoTaskMemAlloc(cbSize);
}

static void WINAPI PMemoryAllocator_Free(PMemoryAllocator *_this, void *pv)
{
    CoTaskMemFree(pv);
}

#ifdef __i386__

#include "pshpack1.h"
typedef struct
{
    BYTE pop_eax;  /* popl  %eax  */
    BYTE push_ecx; /* pushl %ecx  */
    BYTE push_eax; /* pushl %eax  */
    BYTE jmp_func; /* jmp   $func */
    DWORD func;
} THISCALL_TO_STDCALL_THUNK;
#include "poppack.h"

static THISCALL_TO_STDCALL_THUNK *wrapperCodeMem = NULL;

static void fill_thunk(THISCALL_TO_STDCALL_THUNK *thunk, void *fn)
{
    thunk->pop_eax = 0x58;
    thunk->push_ecx = 0x51;
    thunk->push_eax = 0x50;
    thunk->jmp_func = 0xe9;
    thunk->func = (char*)fn - (char*)(&thunk->func + 1);
}

static void setup_vtable(struct _PMemoryAllocator_vtable *vtable)
{
    wrapperCodeMem = VirtualAlloc(NULL, 2 * sizeof(*wrapperCodeMem),
                                  MEM_COMMIT, PAGE_EXECUTE_READWRITE);

    fill_thunk(&wrapperCodeMem[0], PMemoryAllocator_Allocate);
    fill_thunk(&wrapperCodeMem[1], PMemoryAllocator_Free);

    vtable->Allocate = &wrapperCodeMem[0];
    vtable->Free = &wrapperCodeMem[1];
}

#else

static void setup_vtable(struct _PMemoryAllocator_vtable *vtable)
{
    vtable->Allocate = PMemoryAllocator_Allocate;
    vtable->Free = PMemoryAllocator_Free;
}

#endif

static const char serialized_i4[] = {
    3,0, /* VT_I4 */
    0,0, /* padding */
    0xef,0xcd,0xab,0xfe
};

static void test_propertytovariant(void)
{
    HANDLE hcoml2;
    BOOLEAN (__stdcall *pStgConvertPropertyToVariant)(const SERIALIZEDPROPERTYVALUE*,USHORT,PROPVARIANT*,PMemoryAllocator*);
    PROPVARIANT propvar;
    PMemoryAllocator allocator;
    struct _PMemoryAllocator_vtable vtable;
    BOOLEAN ret;

    hcoml2 = LoadLibraryA("coml2");

    if (!hcoml2)
    {
       win_skip("coml2 not available\n");
       return;
    }

    pStgConvertPropertyToVariant = (void*)GetProcAddress(hcoml2, MAKEINTRESOURCEA(5));

    if (!pStgConvertPropertyToVariant)
    {
        win_skip("StgConvertPropertyToVariant not available\n");
        return;
    }

    setup_vtable(&vtable);
    allocator.vt = &vtable;

    ret = pStgConvertPropertyToVariant((SERIALIZEDPROPERTYVALUE*)serialized_i4,
        CP_WINUNICODE, &propvar, &allocator);

    ok(ret == 0, "StgConvertPropertyToVariant returned %i\n", ret);
    ok(propvar.vt == VT_I4, "unexpected vt %x\n", propvar.vt);
    ok(propvar.lVal == 0xfeabcdef, "unexpected lVal %lx\n", propvar.lVal);
}

static void test_varianttoproperty(void)
{
    HANDLE hcoml2;
    PROPVARIANT propvar;
    SERIALIZEDPROPERTYVALUE *propvalue, *own_propvalue;
    SERIALIZEDPROPERTYVALUE* (__stdcall *pStgConvertVariantToProperty)(
        const PROPVARIANT*,USHORT,SERIALIZEDPROPERTYVALUE*,ULONG*,PROPID,BOOLEAN,ULONG*);
    ULONG len;

    hcoml2 = LoadLibraryA("coml2");

    if (!hcoml2)
    {
       win_skip("coml2 not available\n");
       return;
    }

    pStgConvertVariantToProperty = (void*)GetProcAddress(hcoml2, MAKEINTRESOURCEA(4));

    if (!pStgConvertVariantToProperty)
    {
        win_skip("StgConvertVariantToProperty not available\n");
        return;
    }

    own_propvalue = malloc(sizeof(SERIALIZEDPROPERTYVALUE) + 20);

    PropVariantInit(&propvar);

    propvar.vt = VT_I4;
    propvar.lVal = 0xfeabcdef;

    len = 0xdeadbeef;
    propvalue = pStgConvertVariantToProperty(&propvar, CP_WINUNICODE, NULL, &len, 0, FALSE, 0);

    ok(propvalue == NULL, "got nonnull propvalue\n");
    todo_wine ok(len == 8, "unexpected length %ld\n", len);

    if (len == 0xdeadbeef)
    {
        free(own_propvalue);
        return;
    }

    len = 20;
    propvalue = pStgConvertVariantToProperty(&propvar, CP_WINUNICODE, own_propvalue, &len, 0, FALSE, 0);

    ok(propvalue == own_propvalue, "unexpected propvalue %p\n", propvalue);
    ok(len == 8, "unexpected length %ld\n", len);
    ok(!memcmp(propvalue, serialized_i4, 8), "got wrong data\n");

    free(own_propvalue);
}

START_TEST(propvariant)
{
    test_propertytovariant();
    test_varianttoproperty();
}
