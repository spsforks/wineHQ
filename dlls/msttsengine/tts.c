/*
 * MSTTSEngine SAPI engine implementation.
 *
 * Copyright 2023 Shaun Ren for CodeWeavers
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

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"

#include "sapiddk.h"
#include "sperror.h"

#include "flite.h"

#include "wine/debug.h"

#include "initguid.h"

WINE_DEFAULT_DEBUG_CHANNEL(msttsengine);

DEFINE_GUID(SPDFID_WaveFormatEx, 0xc31adbae, 0x527f, 0x4ff5, 0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c);

cst_voice *register_cmu_us_awb(const char *voxdir);

struct ttsengine
{
    ISpTTSEngine ISpTTSEngine_iface;
    ISpObjectWithToken ISpObjectWithToken_iface;
    LONG ref;

    ISpObjectToken *token;
    cst_voice *voice;
};

static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

static inline struct ttsengine *impl_from_ISpTTSEngine(ISpTTSEngine *iface)
{
    return CONTAINING_RECORD(iface, struct ttsengine, ISpTTSEngine_iface);
}

static inline struct ttsengine *impl_from_ISpObjectWithToken(ISpObjectWithToken *iface)
{
    return CONTAINING_RECORD(iface, struct ttsengine, ISpObjectWithToken_iface);
}

static BOOL WINAPI init_tts(INIT_ONCE *once, void *param, void **ctx)
{
    flite_init();
    return TRUE;
}

static HRESULT WINAPI ttsengine_QueryInterface(ISpTTSEngine *iface, REFIID iid, void **obj)
{
    struct ttsengine *This = impl_from_ISpTTSEngine(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(iid), obj);

    if (IsEqualIID(iid, &IID_IUnknown) ||
        IsEqualIID(iid, &IID_ISpTTSEngine))
    {
        *obj = &This->ISpTTSEngine_iface;
    }
    else if (IsEqualIID(iid, &IID_ISpObjectWithToken))
        *obj = &This->ISpObjectWithToken_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI ttsengine_AddRef(ISpTTSEngine *iface)
{
    struct ttsengine *This = impl_from_ISpTTSEngine(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%lu\n", This, ref);

    return ref;
}

static ULONG WINAPI ttsengine_Release(ISpTTSEngine *iface)
{
    struct ttsengine *This = impl_from_ISpTTSEngine(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%lu\n", This, ref);

    if (!ref)
    {
        if (This->token) ISpObjectToken_Release(This->token);
        free(This);
    }

    return ref;
}

static int audio_stream_chunk_cb(const cst_wave *w, int start, int size, int last, cst_audio_streaming_info *asi)
{
    ISpTTSEngineSite *site = asi->userdata;

    if (ISpTTSEngineSite_GetActions(site) & SPVES_ABORT)
        return CST_AUDIO_STREAM_STOP;
    if (FAILED(ISpTTSEngineSite_Write(site, w->samples + start, size * sizeof(w->samples[0]), NULL)))
        return CST_AUDIO_STREAM_STOP;

    return CST_AUDIO_STREAM_CONT;
}

static HRESULT WINAPI ttsengine_Speak(ISpTTSEngine *iface, DWORD flags, REFGUID fmtid,
                                      const WAVEFORMATEX *wfx, const SPVTEXTFRAG *frag_list,
                                      ISpTTSEngineSite *site)
{
    struct ttsengine *This = impl_from_ISpTTSEngine(iface);
    cst_audio_streaming_info *asi;
    char *text = NULL;
    size_t text_len;
    cst_wave *wave;

    TRACE("(%p, %#lx, %s, %p, %p, %p).\n", iface, flags, debugstr_guid(fmtid), wfx, frag_list, site);

    if (!This->voice)
        return SPERR_UNINITIALIZED;

    asi = new_audio_streaming_info();
    asi->asc          = audio_stream_chunk_cb;
    asi->min_buffsize = get_param_int(This->voice->features, "sample_rate", 16000) * 50 / 1000;
    asi->userdata     = site;
    feat_set(This->voice->features, "streaming_info", audio_streaming_info_val(asi));

    for (; frag_list; frag_list = frag_list->pNext)
    {
        if (ISpTTSEngineSite_GetActions(site) & SPVES_ABORT)
            return S_OK;

        text_len = WideCharToMultiByte(CP_UTF8, 0, frag_list->pTextStart, frag_list->ulTextLen, NULL, 0, NULL, NULL);
        if (!(text = malloc(text_len + 1)))
            return E_OUTOFMEMORY;
        WideCharToMultiByte(CP_UTF8, 0, frag_list->pTextStart, frag_list->ulTextLen, text, text_len, NULL, NULL);
        text[text_len] = '\0';

        if (!(wave = flite_text_to_wave(text, This->voice)))
        {
            free(text);
            return E_FAIL;
        }

        delete_wave(wave);
        free(text);
    }

    return S_OK;
}

static HRESULT WINAPI ttsengine_GetOutputFormat(ISpTTSEngine *iface, const GUID *fmtid,
                                                const WAVEFORMATEX *wfx, GUID *out_fmtid,
                                                WAVEFORMATEX **out_wfx)
{
    struct ttsengine *This = impl_from_ISpTTSEngine(iface);

    TRACE("(%p, %s, %p, %p, %p).\n", iface, debugstr_guid(fmtid), wfx, out_fmtid, out_wfx);

    if (!This->voice)
        return SPERR_UNINITIALIZED;

    *out_fmtid = SPDFID_WaveFormatEx;
    if (!(*out_wfx = CoTaskMemAlloc(sizeof(WAVEFORMATEX))))
        return E_OUTOFMEMORY;

    (*out_wfx)->wFormatTag      = WAVE_FORMAT_PCM;
    (*out_wfx)->nChannels       = 1;
    (*out_wfx)->nSamplesPerSec  = get_param_int(This->voice->features, "sample_rate", 16000);
    (*out_wfx)->wBitsPerSample  = 16;
    (*out_wfx)->nBlockAlign     = (*out_wfx)->nChannels * (*out_wfx)->wBitsPerSample / 8;
    (*out_wfx)->nAvgBytesPerSec = (*out_wfx)->nSamplesPerSec * (*out_wfx)->nBlockAlign;
    (*out_wfx)->cbSize          = 0;

    return S_OK;
}

static ISpTTSEngineVtbl ttsengine_vtbl =
{
    ttsengine_QueryInterface,
    ttsengine_AddRef,
    ttsengine_Release,
    ttsengine_Speak,
    ttsengine_GetOutputFormat,
};

static HRESULT WINAPI objwithtoken_QueryInterface(ISpObjectWithToken *iface, REFIID iid, void **obj)
{
    struct ttsengine *This = impl_from_ISpObjectWithToken(iface);

    TRACE("(%p, %s, %p).\n", iface, debugstr_guid(iid), obj);

    return ISpTTSEngine_QueryInterface(&This->ISpTTSEngine_iface, iid, obj);
}

static ULONG WINAPI objwithtoken_AddRef(ISpObjectWithToken *iface)
{
    struct ttsengine *This = impl_from_ISpObjectWithToken(iface);

    TRACE("(%p).\n", iface);

    return ISpTTSEngine_AddRef(&This->ISpTTSEngine_iface);
}

static ULONG WINAPI objwithtoken_Release(ISpObjectWithToken *iface)
{
    struct ttsengine *This = impl_from_ISpObjectWithToken(iface);

    TRACE("(%p).\n", iface);

    return ISpTTSEngine_Release(&This->ISpTTSEngine_iface);
}

static HRESULT WINAPI objwithtoken_SetObjectToken(ISpObjectWithToken *iface, ISpObjectToken *token)
{
    struct ttsengine *This = impl_from_ISpObjectWithToken(iface);

    TRACE("(%p, %p).\n", iface, token);

    if (!token)
        return E_INVALIDARG;
    if (This->token)
        return SPERR_ALREADY_INITIALIZED;

    if (!(This->voice = register_cmu_us_awb(NULL)))
        return E_FAIL;

    ISpObjectToken_AddRef(token);
    This->token = token;
    return S_OK;
}

static HRESULT WINAPI objwithtoken_GetObjectToken(ISpObjectWithToken *iface, ISpObjectToken **token)
{
    struct ttsengine *This = impl_from_ISpObjectWithToken(iface);

    TRACE("(%p, %p).\n", iface, token);

    if (!token)
        return E_POINTER;

    *token = This->token;
    if (*token)
    {
        ISpObjectToken_AddRef(*token);
        return S_OK;
    }
    else
        return S_FALSE;
}

static const ISpObjectWithTokenVtbl objwithtoken_vtbl =
{
    objwithtoken_QueryInterface,
    objwithtoken_AddRef,
    objwithtoken_Release,
    objwithtoken_SetObjectToken,
    objwithtoken_GetObjectToken
};

HRESULT ttsengine_create(REFIID iid, void **obj)
{
    struct ttsengine *This;
    HRESULT hr;

    if (!InitOnceExecuteOnce(&init_once, init_tts, NULL, NULL))
        return E_FAIL;

    if (!(This = malloc(sizeof(*This))))
        return E_OUTOFMEMORY;

    This->ISpTTSEngine_iface.lpVtbl = &ttsengine_vtbl;
    This->ISpObjectWithToken_iface.lpVtbl = &objwithtoken_vtbl;
    This->ref = 1;

    This->token = NULL;
    This->voice = NULL;

    hr = ISpTTSEngine_QueryInterface(&This->ISpTTSEngine_iface, iid, obj);
    ISpTTSEngine_Release(&This->ISpTTSEngine_iface);
    return hr;
}
