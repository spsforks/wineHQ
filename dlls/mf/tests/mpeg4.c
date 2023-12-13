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

static IMFMediaType *h264_video_type;
static IMFMediaType *aac_audio_type;

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

    /* Test shutdown state. */
    hr = IMFMediaSink_Shutdown(sink);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaSink_Shutdown(sink);
    ok(hr == MF_E_SHUTDOWN, "Unexpected hr %#lx.\n", hr);

    hr = IMFStreamSink_GetMediaSink(stream_sink, &sink2);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    IMFMediaSink_Release(sink2);

    hr = IMFStreamSink_GetIdentifier(stream_sink, &id);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = IMFMediaTypeHandler_GetMajorType(type_handler, NULL);
    todo_wine
    ok(hr == E_POINTER, "Unexpected hr %#lx.\n", hr);
    hr = IMFMediaTypeHandler_GetMajorType(type_handler, &guid);
    todo_wine
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

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

START_TEST(mpeg4)
{
    start_tests();

    test_mpeg4_media_sink_create();
    test_mpeg4_media_sink();

    end_tests();
}
