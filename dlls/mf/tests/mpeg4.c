/*
 * Copyright 2023 Ziqing Hui for CodeWeavers
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

#include "mfapi.h"
#include "mferror.h"
#include "mfidl.h"
#include "mmdeviceapi.h"
#include "uuids.h"
#include "wmcodecdsp.h"

#include "mf_test.h"

#include "wine/test.h"

struct test_event_callback
{
    IMFAsyncCallback IMFAsyncCallback_iface;
    HANDLE started, stopped;
};

struct test_finalize_callback
{
    IMFAsyncCallback IMFAsyncCallback_iface;
    HANDLE finalized;
};

static const BYTE test_h264_header[] =
{
    0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x14, 0xac, 0xd9, 0x46, 0x36, 0xc0,
    0x5a, 0x83, 0x03, 0x03, 0x52, 0x80, 0x00, 0x00, 0x03, 0x00, 0x80, 0x00,
    0x00, 0x03, 0x01, 0x47, 0x8a, 0x14, 0xcb, 0x00, 0x00, 0x01, 0x68, 0xeb,
    0xec, 0xb2, 0x2c,
};

static const BYTE test_h264_frame[] =
{
    0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x17, 0xff, 0xe8, 0xff, 0xf2,
    0x3f, 0x9b, 0x0f, 0x5c, 0xdd, 0x08, 0x3f, 0xf5, 0xe8, 0xfc, 0xbb, 0xed,
    0x67, 0xbd, 0x22, 0xa1, 0xd7, 0xba, 0x21, 0xe6, 0x75, 0x8d, 0x3c, 0x11,
    0x12, 0x18, 0xd9, 0x81, 0x11, 0x75, 0x6a, 0x9b, 0x14, 0xcc, 0x50, 0x96,
    0x3f, 0x70, 0xd4, 0xf8, 0x3d, 0x17, 0xc9, 0x4e, 0x23, 0x96, 0x4e, 0x37,
    0xb9, 0xbe, 0x74, 0xf1, 0x53, 0x9f, 0xb4, 0x59, 0x57, 0x32, 0xee, 0x7f,
    0xfd, 0xea, 0x48, 0x2d, 0x80, 0x9e, 0x19, 0x61, 0x59, 0xcb, 0x14, 0xbd,
    0xcd, 0xb3, 0x3e, 0x81, 0x05, 0x56, 0x8e, 0x9c, 0xd9, 0x3f, 0x01, 0x6b,
    0x3e, 0x3c, 0x95, 0xcb, 0xc4, 0x1c, 0xfd, 0xb1, 0x72, 0x23, 0xbb, 0x7b,
    0xf8, 0xb8, 0x50, 0xda, 0x3c, 0x70, 0xc5, 0x7a, 0xc1, 0xe3, 0x13, 0x29,
    0x79, 0x7a, 0xbe, 0xff, 0x5a, 0x26, 0xc3, 0xb6, 0x56, 0xbb, 0x6a, 0x97,
    0x4d, 0xdc, 0x1e, 0x07, 0x4a, 0xaf, 0xff, 0x9e, 0x60, 0x20, 0x69, 0xf9,
    0xfc, 0xe8, 0xe0, 0xa6, 0x10, 0xa3, 0xab, 0x0f, 0xbe, 0x9c, 0x59, 0xa6,
    0xb4, 0x69, 0x4d, 0xc6, 0x09, 0xaa, 0xa8, 0xab, 0xbc, 0x64, 0xfd, 0x7e,
    0xde, 0x5f, 0x55, 0x06, 0xb9, 0xae, 0xce, 0x76, 0x5f, 0x63, 0x3a, 0x12,
    0x2e, 0x9e, 0xbd, 0x28, 0x71, 0x69, 0x34, 0xc9, 0xab, 0x20, 0x28, 0xb8,
    0x4b, 0x20, 0x1c, 0xe1, 0xc8, 0xc4, 0xa6, 0x7d, 0x73, 0x53, 0x73, 0xbf,
    0x21, 0x19, 0x9a, 0xd5, 0xa7, 0xcf, 0x47, 0x5a, 0xda, 0x34, 0x50, 0x7b,
    0x69, 0x8e, 0x52, 0xb2, 0x61, 0xda, 0x8e, 0x20, 0x95, 0x73, 0xc5, 0xb9,
    0x2b, 0x14, 0x48, 0xc1, 0x68, 0x3a, 0x7c, 0x78, 0x14, 0xe9, 0x92, 0xc7,
    0x89, 0xfc, 0x4f, 0x90, 0xaf, 0x54, 0x1e, 0xd0, 0xf0, 0x00, 0x25, 0x3e,
    0xcf, 0xbc, 0x18, 0xad, 0xc9, 0x6b, 0x9d, 0x77, 0x21, 0x6d, 0x5d, 0x2e,
    0xce, 0x09, 0xd9, 0xee, 0x79, 0xb6, 0xe7, 0xe4, 0xf4, 0x7f, 0x6e, 0x11,
    0x7b, 0x32, 0xfb, 0xf6, 0x8c, 0xbf, 0x05, 0xe1, 0x9a, 0x9c, 0x6c, 0x48,
    0x79, 0xac, 0x8f, 0x16, 0xb6, 0xf6, 0x3e, 0x76, 0xab, 0x40, 0x28, 0x61,

};

static IMFMediaType *h264_video_type;
static IMFMediaType *aac_audio_type;

static struct test_event_callback *impl_from_event_callback(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, struct test_event_callback, IMFAsyncCallback_iface);
}

static struct test_finalize_callback *impl_from_finalize_callback(IMFAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, struct test_finalize_callback, IMFAsyncCallback_iface);
}

static HRESULT WINAPI test_callback_QueryInterface(IMFAsyncCallback *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFAsyncCallback) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IMFAsyncCallback_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_callback_AddRef(IMFAsyncCallback *iface)
{
    return 2;
}

static ULONG WINAPI test_callback_Release(IMFAsyncCallback *iface)
{
    return 1;
}

static HRESULT WINAPI test_callback_GetParameters(IMFAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI test_event_callback_Invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    struct test_event_callback *callback = impl_from_event_callback(iface);
    IMFMediaEventGenerator *event_generator;
    IMFMediaEvent *media_event;
    MediaEventType type;
    IUnknown *object;
    HRESULT hr;

    ok(result != NULL, "Unexpected result object.\n");

    hr = IMFAsyncResult_GetState(result, &object);
    ok(hr == S_OK, "GetState returned hr %#lx.\n", hr);
    hr = IUnknown_QueryInterface(object, &IID_IMFMediaEventGenerator, (void **)&event_generator);
    ok(hr == S_OK, "QueryInterface returned hr %#lx.\n", hr);
    hr = IMFMediaEventGenerator_EndGetEvent(event_generator, result, &media_event);
    ok(hr == S_OK, "EndGetEvent returned hr %#lx.\n", hr);

    hr = IMFMediaEvent_GetType(media_event, &type);
    ok(hr == S_OK, "GetType returned hr %#lx.\n", hr);
    switch (type)
    {
        case MEStreamSinkStarted:
            SetEvent(callback->started);
            break;
        case MEStreamSinkStopped:
            SetEvent(callback->stopped);
            break;
        default:
            break;
    }

    hr = IMFMediaEventGenerator_BeginGetEvent(event_generator, iface, object);
    ok(hr == S_OK, "BeginGetEvent returned hr %#lx.\n", hr);

    IMFMediaEvent_Release(media_event);
    IMFMediaEventGenerator_Release(event_generator);
    IUnknown_Release(object);

    return S_OK;
}

static HRESULT WINAPI test_finalize_callback_Invoke(IMFAsyncCallback *iface, IMFAsyncResult *result)
{
    struct test_finalize_callback *callback = impl_from_finalize_callback(iface);
    IMFFinalizableMediaSink *media_sink;
    IUnknown *object;
    HRESULT hr;

    ok(result != NULL, "Unexpected result object.\n");

    hr = IMFAsyncResult_GetState(result, &object);
    ok(hr == S_OK, "GetState returned hr %#lx.\n", hr);
    hr = IUnknown_QueryInterface(object, &IID_IMFFinalizableMediaSink, (void **)&media_sink);
    ok(hr == S_OK, "QueryInterface returned hr %#lx.\n", hr);
    hr = IMFFinalizableMediaSink_EndFinalize(media_sink, result);
    ok(hr == S_OK, "EndFinalize returned hr %#lx.\n", hr);
    IMFFinalizableMediaSink_Release(media_sink);
    IUnknown_Release(object);

    SetEvent(callback->finalized);

    return S_OK;
}

static const IMFAsyncCallbackVtbl test_callback_event_vtbl =
{
    test_callback_QueryInterface,
    test_callback_AddRef,
    test_callback_Release,
    test_callback_GetParameters,
    test_event_callback_Invoke,
};

static const IMFAsyncCallbackVtbl test_callback_finalize_vtbl =
{
    test_callback_QueryInterface,
    test_callback_AddRef,
    test_callback_Release,
    test_callback_GetParameters,
    test_finalize_callback_Invoke,
};

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static void check_interface_(unsigned int line, void *iface_ptr, REFIID iid, BOOL supported)
{
    IUnknown *iface = iface_ptr;
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

static void start_tests(void)
{
    DWORD width = 96, height = 96;
    HRESULT hr;

    hr = CoInitialize(NULL);
    ok(hr == S_OK, "CoInitialize failed, hr %#lx.\n", hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    ok(hr == S_OK, "MFStartup failed, hr %#lx.\n", hr);

    hr = MFCreateMediaType(&h264_video_type);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(h264_video_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(h264_video_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT64(h264_video_type, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT64(h264_video_type, &MF_MT_FRAME_RATE, ((UINT64)30000 << 32) | 1001);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetBlob(h264_video_type, &MF_MT_MPEG_SEQUENCE_HEADER,
            test_h264_sequence_header, sizeof(test_h264_sequence_header));
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = MFCreateMediaType(&aac_audio_type);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(aac_audio_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(aac_audio_type, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AUDIO_NUM_CHANNELS, 1);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12000);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 41);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetUINT32(aac_audio_type, &MF_MT_AAC_PAYLOAD_TYPE, 0);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaType_SetBlob(aac_audio_type, &MF_MT_USER_DATA, test_aac_codec_data, sizeof(test_aac_codec_data));
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
}

static void end_tests(void)
{
    HRESULT hr;

    IMFMediaType_Release(aac_audio_type);
    IMFMediaType_Release(h264_video_type);

    hr = MFShutdown();
    ok(hr == S_OK, "MFShutdown returned %#lx.\n", hr);

    CoUninitialize();
}

static HRESULT create_mpeg4_media_sink(IMFMediaType *video_type, IMFMediaType *audio_type,
        IMFByteStream **bytestream, IMFMediaSink **media_sink)
{
    HRESULT hr;

    hr = MFCreateTempFile(MF_ACCESSMODE_WRITE, MF_OPENMODE_DELETE_IF_EXIST, 0, bytestream);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    if (FAILED(hr = MFCreateMPEG4MediaSink(*bytestream, video_type, audio_type, media_sink)))
    {
        IMFByteStream_Release(*bytestream);
        *bytestream = NULL;
    }

    return hr;
}

static void test_mpeg4_media_sink_create(void)
{
    IMFByteStream *bytestream;
    IMFMediaSink *sink;
    HRESULT hr;

    hr = MFCreateMPEG4MediaSink(NULL, NULL, NULL, NULL);
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);

    sink = (void *)0xdeadbeef;
    hr = MFCreateMPEG4MediaSink(NULL, NULL, NULL, &sink);
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);
    ok(sink == (void *)0xdeadbeef, "Unexpected pointer %p.\n", sink);

    hr = create_mpeg4_media_sink(h264_video_type, aac_audio_type, &bytestream, &sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFByteStream_Release(bytestream);
    IMFMediaSink_Release(sink);

    hr = create_mpeg4_media_sink(h264_video_type, NULL, &bytestream, &sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFByteStream_Release(bytestream);
    IMFMediaSink_Release(sink);

    hr = create_mpeg4_media_sink(NULL, aac_audio_type, &bytestream, &sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFByteStream_Release(bytestream);
    IMFMediaSink_Release(sink);

    hr = create_mpeg4_media_sink(NULL, NULL, &bytestream, &sink);
    ok(hr == S_OK || broken(hr == E_INVALIDARG), "Unexpected hr %#lx.\n", hr);
    if (hr == S_OK)
    {
        IMFByteStream_Release(bytestream);
        IMFMediaSink_Release(sink);
    }
}

static void test_mpeg4_media_sink(void)
{
    IMFMediaSink *sink = NULL, *sink2 = NULL, *sink_audio = NULL, *sink_video = NULL, *sink_empty = NULL;
    IMFByteStream *bytestream, *bytestream_audio, *bytestream_video, *bytestream_empty = NULL;
    IMFMediaTypeHandler *type_handler = NULL;
    IMFPresentationClock *clock;
    IMFStreamSink *stream_sink;
    IMFMediaType *media_type;
    DWORD id, count, flags;
    HRESULT hr;
    GUID guid;

    hr = create_mpeg4_media_sink(h264_video_type, aac_audio_type, &bytestream, &sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = create_mpeg4_media_sink(h264_video_type, NULL, &bytestream_video, &sink_video);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = create_mpeg4_media_sink(NULL, aac_audio_type, &bytestream_audio, &sink_audio);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = create_mpeg4_media_sink(NULL, NULL, &bytestream_empty, &sink_empty);
    ok(hr == S_OK || broken(hr == E_INVALIDARG), "Unexpected hr %#lx.\n", hr);

    /* Test sink. */
    flags = 0xdeadbeef;
    hr = IMFMediaSink_GetCharacteristics(sink, &flags);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    todo_wine
    ok(flags == MEDIASINK_RATELESS || broken(flags == (MEDIASINK_RATELESS | MEDIASINK_FIXED_STREAMS)),
            "Unexpected flags %#lx.\n", flags);

    check_interface(sink, &IID_IMFMediaEventGenerator, TRUE);
    check_interface(sink, &IID_IMFFinalizableMediaSink, TRUE);
    check_interface(sink, &IID_IMFClockStateSink, TRUE);
    todo_wine
    check_interface(sink, &IID_IMFGetService, TRUE);

    /* Test sink stream count. */
    hr = IMFMediaSink_GetStreamSinkCount(sink, NULL);
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaSink_GetStreamSinkCount(sink, &count);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(count == 2, "Unexpected count %lu.\n", count);

    hr = IMFMediaSink_GetStreamSinkCount(sink_audio, &count);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(count == 1, "Unexpected count %lu.\n", count);

    hr = IMFMediaSink_GetStreamSinkCount(sink_video, &count);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(count == 1, "Unexpected count %lu.\n", count);

    if (sink_empty)
    {
        hr = IMFMediaSink_GetStreamSinkCount(sink_empty, &count);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        ok(count == 0, "Unexpected count %lu.\n", count);
    }

    /* Test GetStreamSinkByIndex. */
    hr = IMFMediaSink_GetStreamSinkByIndex(sink_video, 0, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(id == 1, "Unexpected id %lu.\n", id);
    IMFStreamSink_Release(stream_sink);

    hr = IMFMediaSink_GetStreamSinkByIndex(sink_audio, 0, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    ok(id == 2, "Unexpected id %lu.\n", id);
    IMFStreamSink_Release(stream_sink);

    stream_sink = (void *)0xdeadbeef;
    hr = IMFMediaSink_GetStreamSinkByIndex(sink_audio, 1, &stream_sink);
    ok(hr == MF_E_INVALIDINDEX, "Unexpected hr %#lx.\n", hr);
    ok(stream_sink == (void *)0xdeadbeef, "Unexpected pointer %p.\n", stream_sink);

    stream_sink = (void *)0xdeadbeef;
    hr = IMFMediaSink_GetStreamSinkByIndex(sink_video, 1, &stream_sink);
    ok(hr == MF_E_INVALIDINDEX, "Unexpected hr %#lx.\n", hr);
    ok(stream_sink == (void *)0xdeadbeef, "Unexpected pointer %p.\n", stream_sink);

    /* Test GetStreamSinkById. */
    hr = IMFMediaSink_GetStreamSinkById(sink, 1, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFStreamSink_Release(stream_sink);
    hr = IMFMediaSink_GetStreamSinkById(sink, 2, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFStreamSink_Release(stream_sink);
    hr = IMFMediaSink_GetStreamSinkById(sink_video, 1, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFStreamSink_Release(stream_sink);
    hr = IMFMediaSink_GetStreamSinkById(sink_audio, 2, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFStreamSink_Release(stream_sink);

    stream_sink = (void *)0xdeadbeef;
    hr = IMFMediaSink_GetStreamSinkById(sink_video, 2, &stream_sink);
    ok(hr == MF_E_INVALIDSTREAMNUMBER, "Unexpected hr %#lx.\n", hr);
    ok(stream_sink == (void *)0xdeadbeef, "Unexpected pointer %p.\n", stream_sink);

    stream_sink = (void *)0xdeadbeef;
    hr = IMFMediaSink_GetStreamSinkById(sink_audio, 1, &stream_sink);
    ok(hr == MF_E_INVALIDSTREAMNUMBER, "Unexpected hr %#lx.\n", hr);
    ok(stream_sink == (void *)0xdeadbeef, "Unexpected pointer %p.\n", stream_sink);

    /* Test adding and removing stream sink. */
    if (!(flags & MEDIASINK_FIXED_STREAMS))
    {
        hr = IMFMediaSink_AddStreamSink(sink, 123, h264_video_type, &stream_sink);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        IMFStreamSink_Release(stream_sink);
        hr = IMFMediaSink_GetStreamSinkByIndex(sink, 2, &stream_sink);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        ok(id == 123, "Unexpected id %lu.\n", id);
        IMFStreamSink_Release(stream_sink);

        stream_sink = (void *)0xdeadbeef;
        hr = IMFMediaSink_AddStreamSink(sink, 1, aac_audio_type, &stream_sink);
        ok(hr == MF_E_STREAMSINK_EXISTS, "Unexpected hr %#lx.\n", hr);
        ok(!stream_sink, "Unexpected pointer %p.\n", stream_sink);

        hr = IMFMediaSink_RemoveStreamSink(sink, 1);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        hr = IMFMediaSink_AddStreamSink(sink, 1, aac_audio_type, &stream_sink);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        IMFStreamSink_Release(stream_sink);
        hr = IMFMediaSink_GetStreamSinkByIndex(sink, 2, &stream_sink);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        ok(id == 1, "Unexpected id %lu.\n", id);
        IMFStreamSink_Release(stream_sink);

        hr = IMFMediaSink_RemoveStreamSink(sink, 123);
        ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
        hr = IMFMediaSink_RemoveStreamSink(sink, 123);
        ok(hr == MF_E_INVALIDSTREAMNUMBER, "Unexpected hr %#lx.\n", hr);
    }

    /* Test PresentationClock. */
    hr = MFCreatePresentationClock(&clock);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_SetPresentationClock(sink, NULL);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    todo_wine
    hr = IMFMediaSink_SetPresentationClock(sink, clock);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFPresentationClock_Release(clock);

    /* Test stream. */
    hr = IMFMediaSink_GetStreamSinkByIndex(sink_audio, 0, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFStreamSink_GetMediaSink(stream_sink, &sink2);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFMediaSink_Release(sink2);

    check_interface(stream_sink, &IID_IMFMediaEventGenerator, TRUE);
    check_interface(stream_sink, &IID_IMFMediaTypeHandler, TRUE);

    hr = IMFStreamSink_GetMediaTypeHandler(stream_sink, &type_handler);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaTypeHandler_GetMajorType(type_handler, NULL);
    todo_wine
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaTypeHandler_GetMajorType(type_handler, &guid);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    todo_wine
    ok(IsEqualGUID(&guid, &MFMediaType_Audio), "Unexpected major type.\n");

    hr = IMFMediaTypeHandler_GetMediaTypeCount(type_handler, &count);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    todo_wine
    ok(count == 1, "Unexpected count %lu.\n", count);

    hr = IMFMediaTypeHandler_GetCurrentMediaType(type_handler, &media_type);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_NUM_CHANNELS, 1);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaTypeHandler_SetCurrentMediaType(type_handler, NULL);
    todo_wine
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaTypeHandler_SetCurrentMediaType(type_handler, media_type);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    IMFMediaType_Release(media_type);

    IMFMediaTypeHandler_Release(type_handler);
    IMFMediaSink_Release(sink);
    IMFMediaSink_Release(sink_video);
    IMFMediaSink_Release(sink_audio);
    if (sink_empty)
        IMFMediaSink_Release(sink_empty);
    IMFByteStream_Release(bytestream);
    if (bytestream_empty)
        IMFByteStream_Release(bytestream_empty);
    IMFByteStream_Release(bytestream_video);
    IMFByteStream_Release(bytestream_audio);
}

static void test_mpeg4_media_sink_shutdown_state(void)
{
    IMFMediaTypeHandler *type_handler;
    IMFClockStateSink *clock_sink;
    IMFMediaSink *sink, *sink2;
    IMFStreamSink *stream_sink;
    IMFByteStream *bytestream;
    DWORD id, flags;
    HRESULT hr;
    GUID guid;

    hr = create_mpeg4_media_sink(h264_video_type, aac_audio_type, &bytestream, &sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaSink_GetStreamSinkByIndex(sink, 0, &stream_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFStreamSink_GetMediaTypeHandler(stream_sink, &type_handler);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaSink_Shutdown(sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_Shutdown(sink);
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);

    hr = IMFStreamSink_GetMediaSink(stream_sink, &sink2);
    todo_wine
    ok(hr == MF_E_STREAMSINK_REMOVED, "Unexpected hr %#lx.\n", hr);
    hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
    todo_wine
    ok(hr == MF_E_STREAMSINK_REMOVED, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaTypeHandler_GetMajorType(type_handler, NULL);
    todo_wine
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaTypeHandler_GetMajorType(type_handler, &guid);
    todo_wine
    ok(hr == MF_E_STREAMSINK_REMOVED, "Unexpected hr %#lx.\n", hr);

    IMFMediaTypeHandler_Release(type_handler);
    IMFStreamSink_Release(stream_sink);

    hr = IMFMediaSink_AddStreamSink(sink, 0, aac_audio_type, &stream_sink);
    todo_wine
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_GetStreamSinkByIndex(sink, 0, &stream_sink);
    todo_wine
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_GetStreamSinkById(sink, 0, &stream_sink);
    todo_wine
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_GetCharacteristics(sink, &flags);
    todo_wine
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaSink_QueryInterface(sink, &IID_IMFClockStateSink, (void **)&clock_sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockStart(clock_sink, MFGetSystemTime(), 0);
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockStop(clock_sink, MFGetSystemTime());
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockPause(clock_sink, MFGetSystemTime());
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockRestart(clock_sink, MFGetSystemTime());
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockSetRate(clock_sink, MFGetSystemTime(), 1.0f);
    todo_wine
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);
    IMFClockStateSink_Release(clock_sink);

    IMFMediaSink_Release(sink);
    IMFByteStream_Release(bytestream);
}

static void test_mpeg4_media_sink_process(void)
{
    struct test_finalize_callback finalize_callback = {.IMFAsyncCallback_iface.lpVtbl = &test_callback_finalize_vtbl};
    struct test_event_callback event_callback = {.IMFAsyncCallback_iface.lpVtbl = &test_callback_event_vtbl};
    DWORD width = 96, height = 96, fps = 1, ret;
    IMFFinalizableMediaSink *finalizable;
    IMFClockStateSink *clock_sink;
    IMFStreamSink *stream_sink;
    IMFByteStream *bytestream;
    IMFMediaSink *media_sink;
    IMFMediaType *video_type;
    IMFSample *input_sample;
    HRESULT hr;

    hr = MFCreateMediaType(&video_type);
    ok(hr == S_OK, "MFCreateMediaType returned %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(video_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    ok(hr == S_OK, "SetGUID returned %#lx.\n", hr);
    hr = IMFMediaType_SetGUID(video_type, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
    ok(hr == S_OK, "SetGUID returned %#lx.\n", hr);
    hr = IMFMediaType_SetUINT64(video_type, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    ok(hr == S_OK, "SetUINT64 returned %#lx.\n", hr);
    hr = IMFMediaType_SetUINT64(video_type, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    ok(hr == S_OK, "SetUINT64 returned %#lx.\n", hr);
    hr = IMFMediaType_SetBlob(video_type, &MF_MT_MPEG_SEQUENCE_HEADER, test_h264_header, sizeof(test_h264_header));
    ok(hr == S_OK, "etBlob returned %#lx.\n", hr);

    hr = create_mpeg4_media_sink(video_type, NULL, &bytestream, &media_sink);
    ok(hr == S_OK, "Failed to create media sink, hr %#lx.\n", hr);
    IMFMediaType_Release(video_type);

    hr = IMFMediaSink_QueryInterface(media_sink, &IID_IMFClockStateSink, (void **)&clock_sink);
    ok(hr == S_OK, "QueryInterface returned %#lx.\n", hr);

    event_callback.started = CreateEventW(NULL, FALSE, FALSE, NULL);
    event_callback.stopped = CreateEventW(NULL, FALSE, FALSE, NULL);
    finalize_callback.finalized = CreateEventW(NULL, FALSE, FALSE, NULL);

    /* Start streaming. */
    hr = IMFMediaSink_GetStreamSinkById(media_sink, 1, &stream_sink);
    ok(hr == S_OK, "GetStreamSinkById returned %#lx.\n", hr);
    hr = IMFClockStateSink_OnClockStart(clock_sink, MFGetSystemTime(), 0);
    ok(hr == S_OK, "OnClockStar returned %#lx.\n", hr);
    hr = IMFStreamSink_BeginGetEvent(stream_sink, &event_callback.IMFAsyncCallback_iface, (IUnknown *)stream_sink);
    ok(hr == S_OK, "BeginGetEvent returned %#lx.\n", hr);
    ret = WaitForSingleObject(event_callback.started, 3000);
    ok(hr == S_OK, "WaitForSingleObject returned %#lx.\n", ret);

    /* Process sample. */
    input_sample = create_sample(test_h264_frame, sizeof(test_h264_frame));
    hr = IMFSample_SetSampleTime(input_sample, 0);
    ok(hr == S_OK, "SetSampleTime returned %#lx.\n", hr);
    hr = IMFSample_SetSampleDuration(input_sample, 10000000);
    ok(hr == S_OK, "SetSampleDuration returned %#lx.\n", hr);
    hr = IMFStreamSink_ProcessSample(stream_sink, input_sample);
    ok(hr == S_OK, "ProcessSample returned %#lx.\n", hr);
    IMFSample_Release(input_sample);

    /* Wait for stop event to make sure samples have been processed. */
    hr = IMFClockStateSink_OnClockStop(clock_sink, MFGetSystemTime());
    ok(hr == S_OK, "OnClockStop returned %#lx.\n", hr);
    ret = WaitForSingleObject(event_callback.stopped, 3000);
    ok(hr == S_OK, "WaitForSingleObject returned %#lx.\n", ret);

    /* Finalize. */
    hr = IMFMediaSink_QueryInterface(media_sink, &IID_IMFFinalizableMediaSink, (void **)&finalizable);
    ok(hr == S_OK, "QueryInterface returned %#lx.\n", hr);
    hr = IMFFinalizableMediaSink_BeginFinalize(finalizable,
            &finalize_callback.IMFAsyncCallback_iface, (IUnknown *)media_sink);
    ok(hr == S_OK, "BeginFinalize returned %#lx.\n", hr);
    ret = WaitForSingleObject(finalize_callback.finalized, 3000);
    ok(hr == S_OK, "WaitForSingleObject returned %#lx.\n", ret);
    hr = IMFMediaSink_Shutdown(media_sink);
    ok(hr == S_OK, "Shutdown returned %#lx.\n", hr);
    IMFFinalizableMediaSink_Release(finalizable);

    IMFStreamSink_Release(stream_sink);
    CloseHandle(finalize_callback.finalized);
    CloseHandle(event_callback.stopped);
    CloseHandle(event_callback.started);
    IMFClockStateSink_Release(clock_sink);
    ret = IMFMediaSink_Release(media_sink);
    todo_wine
    ok(ret == 0, "Release returned %lu.\n", ret);
    IMFByteStream_Release(bytestream);
}

START_TEST(mpeg4)
{
    start_tests();

    test_mpeg4_media_sink_create();
    test_mpeg4_media_sink();
    test_mpeg4_media_sink_shutdown_state();
    test_mpeg4_media_sink_process();

    end_tests();
}
