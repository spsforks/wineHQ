/*
 * Copyright 2010 Maarten Lankhorst for CodeWeavers
 * Copyright 2011 Andrew Eikum for CodeWeavers
 * Copyright 2022 Huw Davies
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

#include <stdarg.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "winternl.h"
#include "propsys.h"
#include "propkey.h"
#include "initguid.h"
#include "ole2.h"
#include "mmdeviceapi.h"
#include "devpkey.h"
#include "mmsystem.h"
#include "dsound.h"

#include "endpointvolume.h"
#include "audioclient.h"
#include "audiopolicy.h"

#include "wine/debug.h"
#include "wine/list.h"
#include "wine/unixlib.h"

#include "unixlib.h"

#include "../mmdevapi/mmdevdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(alsa);

#define NULL_PTR_ERR MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, RPC_X_NULL_REF_POINTER)

static CRITICAL_SECTION g_sessions_lock;
static CRITICAL_SECTION_DEBUG g_sessions_lock_debug =
{
    0, 0, &g_sessions_lock,
    { &g_sessions_lock_debug.ProcessLocksList, &g_sessions_lock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_sessions_lock") }
};
static CRITICAL_SECTION g_sessions_lock = { &g_sessions_lock_debug, -1, 0, 0, 0, 0 };
static struct list g_sessions = LIST_INIT(g_sessions);

static WCHAR drv_key_devicesW[256];
static const WCHAR guidW[] = {'g','u','i','d',0};

static const IAudioClient3Vtbl AudioClient3_Vtbl;
extern const IAudioRenderClientVtbl AudioRenderClient_Vtbl;
extern const IAudioCaptureClientVtbl AudioCaptureClient_Vtbl;
extern const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl;
extern const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl;
extern const IAudioClockVtbl AudioClock_Vtbl;
extern const IAudioClock2Vtbl AudioClock2_Vtbl;
extern const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl;
extern const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl;

extern struct audio_session_wrapper *session_wrapper_create(
    struct audio_client *client) DECLSPEC_HIDDEN;

void DECLSPEC_HIDDEN sessions_lock(void)
{
    EnterCriticalSection(&g_sessions_lock);
}

void DECLSPEC_HIDDEN sessions_unlock(void)
{
    LeaveCriticalSection(&g_sessions_lock);
}

static inline ACImpl *impl_from_IAudioClient3(IAudioClient3 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClient3_iface);
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, void *reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        WCHAR buf[MAX_PATH];
        WCHAR *filename;

        if(__wine_init_unix_call()) return FALSE;

        GetModuleFileNameW(dll, buf, ARRAY_SIZE(buf));

        filename = wcsrchr(buf, '\\');
        filename = filename ? filename + 1 : buf;

        swprintf(drv_key_devicesW, ARRAY_SIZE(drv_key_devicesW),
                 L"Software\\Wine\\Drivers\\%s\\devices", filename);

        break;
    }
    case DLL_PROCESS_DETACH:
        if (reserved) break;
        DeleteCriticalSection(&g_sessions_lock);
        break;
    }
    return TRUE;
}

static void set_device_guid(EDataFlow flow, HKEY drv_key, const WCHAR *key_name,
        GUID *guid)
{
    HKEY key;
    BOOL opened = FALSE;
    LONG lr;

    if(!drv_key){
        lr = RegCreateKeyExW(HKEY_CURRENT_USER, drv_key_devicesW, 0, NULL, 0, KEY_WRITE,
                    NULL, &drv_key, NULL);
        if(lr != ERROR_SUCCESS){
            ERR("RegCreateKeyEx(drv_key) failed: %lu\n", lr);
            return;
        }
        opened = TRUE;
    }

    lr = RegCreateKeyExW(drv_key, key_name, 0, NULL, 0, KEY_WRITE,
                NULL, &key, NULL);
    if(lr != ERROR_SUCCESS){
        ERR("RegCreateKeyEx(%s) failed: %lu\n", wine_dbgstr_w(key_name), lr);
        goto exit;
    }

    lr = RegSetValueExW(key, guidW, 0, REG_BINARY, (BYTE*)guid,
                sizeof(GUID));
    if(lr != ERROR_SUCCESS)
        ERR("RegSetValueEx(%s\\guid) failed: %lu\n", wine_dbgstr_w(key_name), lr);

    RegCloseKey(key);
exit:
    if(opened)
        RegCloseKey(drv_key);
}

static void get_device_guid(EDataFlow flow, const char *device, GUID *guid)
{
    HKEY key = NULL, dev_key;
    DWORD type, size = sizeof(*guid);
    WCHAR key_name[256];

    if(flow == eCapture)
        key_name[0] = '1';
    else
        key_name[0] = '0';
    key_name[1] = ',';
    MultiByteToWideChar(CP_UNIXCP, 0, device, -1, key_name + 2, ARRAY_SIZE(key_name) - 2);

    if(RegOpenKeyExW(HKEY_CURRENT_USER, drv_key_devicesW, 0, KEY_WRITE|KEY_READ, &key) == ERROR_SUCCESS){
        if(RegOpenKeyExW(key, key_name, 0, KEY_READ, &dev_key) == ERROR_SUCCESS){
            if(RegQueryValueExW(dev_key, guidW, 0, &type,
                        (BYTE*)guid, &size) == ERROR_SUCCESS){
                if(type == REG_BINARY){
                    RegCloseKey(dev_key);
                    RegCloseKey(key);
                    return;
                }
                ERR("Invalid type for device %s GUID: %lu; ignoring and overwriting\n",
                        wine_dbgstr_w(key_name), type);
            }
            RegCloseKey(dev_key);
        }
    }

    CoCreateGuid(guid);

    set_device_guid(flow, key, key_name, guid);

    if(key)
        RegCloseKey(key);
}

HRESULT WINAPI AUDDRV_GetEndpointIDs(EDataFlow flow, WCHAR ***ids_out, GUID **guids_out,
        UINT *num, UINT *def_index)
{
    struct get_endpoint_ids_params params;
    unsigned int i;
    GUID *guids = NULL;
    WCHAR **ids = NULL;

    TRACE("%d %p %p %p %p\n", flow, ids, guids, num, def_index);

    params.flow = flow;
    params.size = 1000;
    params.endpoints = NULL;
    do{
        HeapFree(GetProcessHeap(), 0, params.endpoints);
        params.endpoints = HeapAlloc(GetProcessHeap(), 0, params.size);
        ALSA_CALL(get_endpoint_ids, &params);
    }while(params.result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    if(FAILED(params.result)) goto end;

    ids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, params.num * sizeof(*ids));
    guids = HeapAlloc(GetProcessHeap(), 0, params.num * sizeof(*guids));
    if(!ids || !guids){
        params.result = E_OUTOFMEMORY;
        goto end;
    }

    for(i = 0; i < params.num; i++){
        WCHAR *name = (WCHAR *)((char *)params.endpoints + params.endpoints[i].name);
        char *device = (char *)params.endpoints + params.endpoints[i].device;
        unsigned int size = (wcslen(name) + 1) * sizeof(WCHAR);

        ids[i] = HeapAlloc(GetProcessHeap(), 0, size);
        if(!ids[i]){
            params.result = E_OUTOFMEMORY;
            goto end;
        }
        memcpy(ids[i], name, size);
        get_device_guid(flow, device, guids + i);
    }
    *def_index = params.default_idx;

end:
    HeapFree(GetProcessHeap(), 0, params.endpoints);
    if(FAILED(params.result)){
        HeapFree(GetProcessHeap(), 0, guids);
        if(ids){
            for(i = 0; i < params.num; i++)
                HeapFree(GetProcessHeap(), 0, ids[i]);
            HeapFree(GetProcessHeap(), 0, ids);
        }
    }else{
        *ids_out = ids;
        *guids_out = guids;
        *num = params.num;
    }

    return params.result;
}

static BOOL get_alsa_name_by_guid(GUID *guid, char *name, DWORD name_size, EDataFlow *flow)
{
    HKEY devices_key;
    UINT i = 0;
    WCHAR key_name[256];
    DWORD key_name_size;

    if(RegOpenKeyExW(HKEY_CURRENT_USER, drv_key_devicesW, 0, KEY_READ, &devices_key) != ERROR_SUCCESS){
        ERR("No devices found in registry?\n");
        return FALSE;
    }

    while(1){
        HKEY key;
        DWORD size, type;
        GUID reg_guid;

        key_name_size = ARRAY_SIZE(key_name);
        if(RegEnumKeyExW(devices_key, i++, key_name, &key_name_size, NULL,
                NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        if(RegOpenKeyExW(devices_key, key_name, 0, KEY_READ, &key) != ERROR_SUCCESS){
            WARN("Couldn't open key: %s\n", wine_dbgstr_w(key_name));
            continue;
        }

        size = sizeof(reg_guid);
        if(RegQueryValueExW(key, guidW, 0, &type,
                    (BYTE*)&reg_guid, &size) == ERROR_SUCCESS){
            if(IsEqualGUID(&reg_guid, guid)){
                RegCloseKey(key);
                RegCloseKey(devices_key);

                TRACE("Found matching device key: %s\n", wine_dbgstr_w(key_name));

                if(key_name[0] == '0')
                    *flow = eRender;
                else if(key_name[0] == '1')
                    *flow = eCapture;
                else{
                    ERR("Unknown device type: %c\n", key_name[0]);
                    return FALSE;
                }

                WideCharToMultiByte(CP_UNIXCP, 0, key_name + 2, -1, name, name_size, NULL, NULL);

                return TRUE;
            }
        }

        RegCloseKey(key);
    }

    RegCloseKey(devices_key);

    WARN("No matching device in registry for GUID %s\n", debugstr_guid(guid));

    return FALSE;
}

HRESULT WINAPI AUDDRV_GetAudioEndpoint(GUID *guid, IMMDevice *dev, IAudioClient **out)
{
    ACImpl *This;
    char alsa_name[256];
    EDataFlow dataflow;
    HRESULT hr;
    int len;

    TRACE("%s %p %p\n", debugstr_guid(guid), dev, out);

    if(!get_alsa_name_by_guid(guid, alsa_name, sizeof(alsa_name), &dataflow))
        return AUDCLNT_E_DEVICE_INVALIDATED;

    if(dataflow != eRender && dataflow != eCapture)
        return E_UNEXPECTED;

    len = strlen(alsa_name);
    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, offsetof(ACImpl, device_name[len + 1]));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioClient3_iface.lpVtbl = &AudioClient3_Vtbl;
    This->IAudioRenderClient_iface.lpVtbl = &AudioRenderClient_Vtbl;
    This->IAudioCaptureClient_iface.lpVtbl = &AudioCaptureClient_Vtbl;
    This->IAudioClock_iface.lpVtbl = &AudioClock_Vtbl;
    This->IAudioClock2_iface.lpVtbl = &AudioClock2_Vtbl;
    This->IAudioStreamVolume_iface.lpVtbl = &AudioStreamVolume_Vtbl;

    hr = CoCreateFreeThreadedMarshaler((IUnknown *)&This->IAudioClient3_iface, &This->marshal);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, This);
        return hr;
    }

    This->dataflow = dataflow;
    memcpy(This->device_name, alsa_name, len + 1);

    This->parent = dev;
    IMMDevice_AddRef(This->parent);

    *out = (IAudioClient *)&This->IAudioClient3_iface;
    IAudioClient3_AddRef(&This->IAudioClient3_iface);

    return S_OK;
}

extern HRESULT WINAPI client_QueryInterface(IAudioClient3 *iface,
        REFIID riid, void **ppv);

extern ULONG WINAPI client_AddRef(IAudioClient3 *iface);

extern ULONG WINAPI client_Release(IAudioClient3 *iface);

static void session_init_vols(AudioSession *session, UINT channels)
{
    if(session->channel_count < channels){
        UINT i;

        if(session->channel_vols)
            session->channel_vols = HeapReAlloc(GetProcessHeap(), 0,
                    session->channel_vols, sizeof(float) * channels);
        else
            session->channel_vols = HeapAlloc(GetProcessHeap(), 0,
                    sizeof(float) * channels);
        if(!session->channel_vols)
            return;

        for(i = session->channel_count; i < channels; ++i)
            session->channel_vols[i] = 1.f;

        session->channel_count = channels;
    }
}

static AudioSession *create_session(const GUID *guid, IMMDevice *device,
        UINT num_channels)
{
    AudioSession *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AudioSession));
    if(!ret)
        return NULL;

    memcpy(&ret->guid, guid, sizeof(GUID));

    ret->device = device;

    list_init(&ret->clients);

    list_add_head(&g_sessions, &ret->entry);

    session_init_vols(ret, num_channels);

    ret->master_vol = 1.f;

    return ret;
}

/* if channels == 0, then this will return or create a session with
 * matching dataflow and GUID. otherwise, channels must also match */
HRESULT get_audio_session(const GUID *sessionguid,
        IMMDevice *device, UINT channels, AudioSession **out)
{
    AudioSession *session;

    if(!sessionguid || IsEqualGUID(sessionguid, &GUID_NULL)){
        *out = create_session(&GUID_NULL, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;

        return S_OK;
    }

    *out = NULL;
    LIST_FOR_EACH_ENTRY(session, &g_sessions, AudioSession, entry){
        if(session->device == device &&
                IsEqualGUID(sessionguid, &session->guid)){
            session_init_vols(session, channels);
            *out = session;
            break;
        }
    }

    if(!*out){
        *out = create_session(sessionguid, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;
    }

    return S_OK;
}

extern HRESULT WINAPI client_Initialize(IAudioClient3 *iface,
        AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
        REFERENCE_TIME period, const WAVEFORMATEX *fmt,
        const GUID *sessionguid);

extern HRESULT WINAPI client_GetBufferSize(IAudioClient3 *iface,
        UINT32 *out);

extern HRESULT WINAPI client_GetStreamLatency(IAudioClient3 *iface,
        REFERENCE_TIME *latency);

extern HRESULT WINAPI client_GetCurrentPadding(IAudioClient3 *iface,
        UINT32 *out);

extern HRESULT WINAPI client_IsFormatSupported(IAudioClient3 *iface,
        AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *fmt,
        WAVEFORMATEX **out);

extern HRESULT WINAPI client_GetMixFormat(IAudioClient3 *iface,
        WAVEFORMATEX **pwfx);

extern HRESULT WINAPI client_GetDevicePeriod(IAudioClient3 *iface,
        REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod);

extern HRESULT WINAPI client_Start(IAudioClient3 *iface);

extern HRESULT WINAPI client_Stop(IAudioClient3 *iface);

extern HRESULT WINAPI client_Reset(IAudioClient3 *iface);

extern HRESULT WINAPI client_SetEventHandle(IAudioClient3 *iface,
        HANDLE event);

extern HRESULT WINAPI client_GetService(IAudioClient3 *iface, REFIID riid,
        void **ppv);

extern HRESULT WINAPI client_IsOffloadCapable(IAudioClient3 *iface,
        AUDIO_STREAM_CATEGORY category, BOOL *offload_capable);

extern HRESULT WINAPI client_SetClientProperties(IAudioClient3 *iface,
        const AudioClientProperties *prop);

extern HRESULT WINAPI client_GetBufferSizeLimits(IAudioClient3 *iface,
        const WAVEFORMATEX *format, BOOL event_driven, REFERENCE_TIME *min_duration,
        REFERENCE_TIME *max_duration);

extern HRESULT WINAPI client_GetSharedModeEnginePeriod(IAudioClient3 *iface,
        const WAVEFORMATEX *format, UINT32 *default_period_frames, UINT32 *unit_period_frames,
        UINT32 *min_period_frames, UINT32 *max_period_frames);

extern HRESULT WINAPI client_GetCurrentSharedModeEnginePeriod(IAudioClient3 *iface,
        WAVEFORMATEX **cur_format, UINT32 *cur_period_frames);

extern HRESULT WINAPI client_InitializeSharedAudioStream(IAudioClient3 *iface,
        DWORD flags, UINT32 period_frames, const WAVEFORMATEX *format,
        const GUID *session_guid);

static const IAudioClient3Vtbl AudioClient3_Vtbl =
{
    client_QueryInterface,
    client_AddRef,
    client_Release,
    client_Initialize,
    client_GetBufferSize,
    client_GetStreamLatency,
    client_GetCurrentPadding,
    client_IsFormatSupported,
    client_GetMixFormat,
    client_GetDevicePeriod,
    client_Start,
    client_Stop,
    client_Reset,
    client_SetEventHandle,
    client_GetService,
    client_IsOffloadCapable,
    client_SetClientProperties,
    client_GetBufferSizeLimits,
    client_GetSharedModeEnginePeriod,
    client_GetCurrentSharedModeEnginePeriod,
    client_InitializeSharedAudioStream,
};

HRESULT WINAPI AUDDRV_GetAudioSessionWrapper(const GUID *guid, IMMDevice *device,
                                             AudioSessionWrapper **out)
{
    AudioSession *session;

    HRESULT hr = get_audio_session(guid, device, 0, &session);
    if(FAILED(hr))
        return hr;

    *out = session_wrapper_create(NULL);
    if(!*out)
        return E_OUTOFMEMORY;

    (*out)->session = session;

    return S_OK;
}

HRESULT WINAPI AUDDRV_GetPropValue(GUID *guid, const PROPERTYKEY *prop, PROPVARIANT *out)
{
    struct get_prop_value_params params;
    char name[256];
    EDataFlow flow;
    unsigned int size = 0;

    TRACE("%s, (%s,%lu), %p\n", wine_dbgstr_guid(guid), wine_dbgstr_guid(&prop->fmtid), prop->pid, out);

    if(!get_alsa_name_by_guid(guid, name, sizeof(name), &flow))
    {
        WARN("Unknown interface %s\n", debugstr_guid(guid));
        return E_NOINTERFACE;
    }

    params.device = name;
    params.flow = flow;
    params.guid = guid;
    params.prop = prop;
    params.value = out;
    params.buffer = NULL;
    params.buffer_size = &size;

    while(1) {
        ALSA_CALL(get_prop_value, &params);

        if(params.result != E_NOT_SUFFICIENT_BUFFER)
            break;

        CoTaskMemFree(params.buffer);
        params.buffer = CoTaskMemAlloc(*params.buffer_size);
        if(!params.buffer)
            return E_OUTOFMEMORY;
    }
    if(FAILED(params.result))
        CoTaskMemFree(params.buffer);

    return params.result;
}