/*
 * PatchAPI
 *
 * Copyright 2011 David Hedberg for CodeWeavers
 * Copyright 2019 Conor McCarthy (implementations)
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
 *
 * TODO
 *  - Special processing of 32-bit executables is not supported, so this
 *    version cannot patch 32-bit .exe and .dll files. See pa19.c for details.
 *  - Implement interleaved decoding when PATCH_OPTION_INTERLEAVE_FILES was
 *    used or the old file exceeds the lzxd window size.
 *  - APPLY_OPTION_FAIL_IF_CLOSE is ignored. Normalization of 32-bit PE files
 *    is required for checking this.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winternl.h"
#include "patchapi.h"
#include "wine/debug.h"

#include "md5.h"
#include "pa19.h"

WINE_DEFAULT_DEBUG_CHANNEL(mspatcha);


static WCHAR *strdupAW(const char *src)
{
    WCHAR *dst = NULL;
    if (src)
    {
        int len = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
        if ((dst = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR))))
            MultiByteToWideChar(CP_ACP, 0, src, -1, dst, len);
    }
    return dst;
}

static inline char nibble2char(unsigned char n)
{
    return (char)((n) < 0xA) ? ('0' + (n)) : ('a' + ((n) - 0xA));
}

static inline void bin2hex(const unsigned char *bin, char *hexstr, size_t maxcount)
{
    size_t i, n = 0;
    for (i = 0; i < maxcount; i++) {
        hexstr[n++] = nibble2char((bin[i] >> 4) & 0xf);
        hexstr[n++] = nibble2char((bin[i] & 0xf));
    }
    hexstr[n] = '\0';
}

static inline void dword2hex(unsigned int value, char *hexstr)
{
    size_t i;
    for (i = 8; i > 0; --i, value >>= 4) {
        hexstr[i-1] = nibble2char((value & 0xf));
    }
    hexstr[8] = '\0';
}

/*****************************************************
 *    TestApplyPatchToFileA (MSPATCHA.@)
 */
BOOL WINAPI TestApplyPatchToFileA(
    LPCSTR patch_file, LPCSTR old_file, ULONG apply_option_flags)
{
    BOOL ret;
    WCHAR *patch_fileW, *old_fileW = NULL;

    if (!(patch_fileW = strdupAW(patch_file))) return FALSE;
    if (old_file && !(old_fileW = strdupAW(old_file)))
    {
        HeapFree(GetProcessHeap(), 0, patch_fileW);
        return FALSE;
    }

    ret = apply_patch_to_file(patch_fileW, old_fileW, NULL,
        apply_option_flags | APPLY_OPTION_TEST_ONLY, NULL, NULL);

    HeapFree(GetProcessHeap(), 0, patch_fileW);
    HeapFree(GetProcessHeap(), 0, old_fileW);
    return ret;
}

BOOL WINAPI TestApplyPatchToFileW(
    LPCWSTR patch_file_name, LPCWSTR old_file_name, ULONG apply_option_flags)
{
    return apply_patch_to_file(patch_file_name, old_file_name, NULL,
        apply_option_flags | APPLY_OPTION_TEST_ONLY, NULL, NULL);
}

BOOL WINAPI TestApplyPatchToFileByHandles(
    HANDLE patch_file_hndl, HANDLE old_file_hndl, ULONG apply_option_flags)
{
    return apply_patch_to_file_by_handles(patch_file_hndl, old_file_hndl, NULL,
        apply_option_flags | APPLY_OPTION_TEST_ONLY, NULL, NULL);
}

BOOL WINAPI TestApplyPatchToFileByBuffers(
    BYTE *patch_file_buf, ULONG patch_file_size,
    BYTE *old_file_buf, ULONG old_file_size,
    ULONG* new_file_size, ULONG apply_option_flags)
{
    /* NOTE: windows preserves last error on success for this function, but no apps are known to depend on it */

    DWORD err = apply_patch_to_file_by_buffers(
                    patch_file_buf, patch_file_size,
                    old_file_buf, old_file_size,
                    NULL, 0, new_file_size, NULL,
                    apply_option_flags | APPLY_OPTION_TEST_ONLY,
                    NULL, NULL);

    SetLastError(err);

    return err == ERROR_SUCCESS;
}

/*****************************************************
 *    ApplyPatchToFileExA (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileExA(
    LPCSTR patch_file, LPCSTR old_file, LPCSTR new_file, ULONG apply_option_flags,
    PPATCH_PROGRESS_CALLBACK progress_fn, PVOID progress_ctx)
{
    BOOL ret = FALSE;
    WCHAR *patch_fileW, *new_fileW, *old_fileW = NULL;

    if (!(patch_fileW = strdupAW(patch_file))) return FALSE;

    if (old_file && !(old_fileW = strdupAW(old_file)))
        goto free_wstrs;

    if (!(new_fileW = strdupAW(new_file)))
        goto free_wstrs;

    ret = apply_patch_to_file(patch_fileW, old_fileW, new_fileW,
                    apply_option_flags, progress_fn, progress_ctx);

    HeapFree(GetProcessHeap(), 0, new_fileW);
free_wstrs:
    HeapFree(GetProcessHeap(), 0, patch_fileW);
    HeapFree(GetProcessHeap(), 0, old_fileW);
    return ret;
}

/*****************************************************
 *    ApplyPatchToFileA (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileA(
    LPCSTR patch_file, LPCSTR old_file, LPCSTR new_file, ULONG apply_flags)
{
    return ApplyPatchToFileExA(patch_file, old_file, new_file, apply_flags, NULL, NULL);
}

/*****************************************************
 *    ApplyPatchToFileW (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileW(
    LPCWSTR patch_file_name, LPCWSTR old_file_name, LPCWSTR new_file_name, ULONG apply_option_flags)
{
    return apply_patch_to_file(patch_file_name, old_file_name, new_file_name,
        apply_option_flags, NULL, NULL);
}

/*****************************************************
 *    ApplyPatchToFileByHandles (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileByHandles(HANDLE patch_file_hndl, HANDLE old_file_hndl, HANDLE new_file_hndl,
    ULONG apply_option_flags)
{
    return apply_patch_to_file_by_handles(patch_file_hndl, old_file_hndl, new_file_hndl,
        apply_option_flags, NULL, NULL);
}

/*****************************************************
 *    ApplyPatchToFileExW (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileExW(
    LPCWSTR patch_file_name, LPCWSTR old_file_name, LPCWSTR new_file_name,
    ULONG apply_option_flags, PPATCH_PROGRESS_CALLBACK progress_fn, PVOID progress_ctx)
{
    return apply_patch_to_file(patch_file_name, old_file_name, new_file_name,
        apply_option_flags, progress_fn, progress_ctx);
}

/*****************************************************
 *    ApplyPatchToFileByHandlesEx (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileByHandlesEx(
    HANDLE patch_file_hndl, HANDLE old_file_hndl, HANDLE new_file_hndl,
    ULONG apply_option_flags, PPATCH_PROGRESS_CALLBACK progress_fn, PVOID progress_ctx)
{
    return apply_patch_to_file_by_handles(patch_file_hndl, old_file_hndl, new_file_hndl,
        apply_option_flags, progress_fn, progress_ctx);
}

/*****************************************************
 *    ApplyPatchToFileByBuffers (MSPATCHA.@)
 */
BOOL WINAPI ApplyPatchToFileByBuffers(
    PBYTE patch_file_view, ULONG patch_file_size, PBYTE old_file_view, ULONG  old_file_size,
    PBYTE* new_file_buf, ULONG new_file_buf_size, ULONG* new_file_size, FILETIME* new_file_time,
    ULONG  apply_option_flags, PPATCH_PROGRESS_CALLBACK progress_fn, PVOID progress_ctx)
{
    /* NOTE: windows preserves last error on success for this function, but no apps are known to depend on it */

    DWORD err = apply_patch_to_file_by_buffers(patch_file_view, patch_file_size,
        old_file_view, old_file_size,
        new_file_buf, new_file_buf_size, new_file_size, new_file_time,
        apply_option_flags, progress_fn, progress_ctx);

    SetLastError(err);

    return err == ERROR_SUCCESS;
}

/*****************************************************
 *    GetFilePatchSignatureA (MSPATCHA.@)
 */
BOOL WINAPI GetFilePatchSignatureA(
    LPCSTR filename, ULONG option_flags, PVOID option_data,
    ULONG ignore_range_count, PPATCH_IGNORE_RANGE ignore_range_array,
    ULONG retain_range_count, PPATCH_RETAIN_RANGE retain_range_array,
    ULONG signature_bufsize, LPSTR signature_buf)
{
    BOOL success = FALSE;
    HANDLE file_hndl;

    file_hndl = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file_hndl != INVALID_HANDLE_VALUE)
    {
        success = GetFilePatchSignatureByHandle(
                    file_hndl, option_flags,option_data,
                    ignore_range_count, ignore_range_array,
                    retain_range_count, retain_range_array,
                    signature_bufsize, signature_buf);

        CloseHandle(file_hndl);
    }

    return success;
}

/*****************************************************
 *    GetFilePatchSignatureW (MSPATCHA.@)
 */
BOOL WINAPI GetFilePatchSignatureW(
    LPCWSTR filename, ULONG option_flags, PVOID option_data,
    ULONG ignore_range_count, PPATCH_IGNORE_RANGE ignore_range_array,
    ULONG retain_range_count, PPATCH_RETAIN_RANGE retain_range_array,
    ULONG signature_bufsize, LPWSTR signature_buf)
{
    BOOL success = FALSE;
    HANDLE file_hndl;
    char ascii_buffer[40];

    file_hndl = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file_hndl != INVALID_HANDLE_VALUE) 
    {
        success = GetFilePatchSignatureByHandle(
                    file_hndl, option_flags,option_data,
                    ignore_range_count, ignore_range_array,
                    retain_range_count, retain_range_array,
                    sizeof(ascii_buffer), ascii_buffer);

        if (success) {
            if ((signature_bufsize / sizeof(WCHAR)) >= (strlen(ascii_buffer) + 1)) {
                success = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED,
                    ascii_buffer, -1, signature_buf, signature_bufsize / sizeof(WCHAR)) != 0;

            } else {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                success = FALSE;
            }
        }

        CloseHandle(file_hndl);
    }

    return success;
}

/*****************************************************
 *    GetFilePatchSignatureByHandle (MSPATCHA.@)
 */
BOOL WINAPI GetFilePatchSignatureByHandle(
    HANDLE file_handle, ULONG option_flags, PVOID option_data,
    ULONG ignore_range_count, PPATCH_IGNORE_RANGE ignore_range_array,
    ULONG retain_range_count, PPATCH_RETAIN_RANGE retain_range_array,
    ULONG signature_bufsize, LPSTR signature_buf)
{
    BOOL success = FALSE;
    HANDLE file_writable_mapping = NULL;
    PVOID file_writable_buf = NULL;
    DWORD file_size = 0;
    DWORD file_size_hi = 0;

    file_size = GetFileSize(file_handle, &file_size_hi);

    /* Cannot support files over 4GiB in size. */
    if (file_size == 0xFFFFFFFF) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_FILE_TOO_LARGE);
        }
        return FALSE;

    } else if (file_size_hi != 0) {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }

    /* No file size? Nothing to do; return success.*/
    if (file_size == 0) {
        return TRUE;
    }

    /* Create a writable file mapping for the given file handle. */
    file_writable_mapping = CreateFileMappingA(file_handle, NULL, PAGE_WRITECOPY, 0, 0, NULL);
    if (file_writable_mapping) {
        file_writable_buf = MapViewOfFile(file_writable_mapping, FILE_MAP_COPY, 0, 0, 0);
        CloseHandle(file_writable_mapping);
        if (file_writable_buf) {
            success = TRUE;
        }
    }

    if (success)
    {
        /* Get the file patch signature for the mapped file. */
        success = GetFilePatchSignatureByBuffer(
                    file_writable_buf, file_size,
                    option_flags, option_data,
                    ignore_range_count, ignore_range_array,
                    retain_range_count, retain_range_array,
                    signature_bufsize, signature_buf);

        /* Unmapped the writable file buffer. */
        UnmapViewOfFile(file_writable_buf);
    }

    /* Handle errors appropriately. */
    if (!success) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_EXTENDED_ERROR);
        }
    }

    return success;
}

/*****************************************************
 *    GetFilePatchSignatureByBuffer (MSPATCHA.@)
 */
BOOL WINAPI GetFilePatchSignatureByBuffer(
    PBYTE file_buffer, ULONG file_size,
    ULONG option_flags, PVOID option_data,
    ULONG ignore_range_count, PPATCH_IGNORE_RANGE ignore_range_array,
    ULONG retain_range_count, PPATCH_RETAIN_RANGE retain_range_array,
    ULONG signature_bufsize, LPSTR signature_buf)
{
    BOOL success;
    INT result;
    UINT32 filecrc;
    unsigned char filehash[MD5DIGESTLEN];

    TRACE("getting file patch signature for buffer 0x%p of size 0x%lX", file_buffer, file_size);

    /* Normalize the given mapped file image. */
    result = NormalizeFileForPatchSignature(
        file_buffer, file_size,
        option_flags, option_data,
        0x10000000, 0x10000000,
        ignore_range_count, ignore_range_array,
        retain_range_count, retain_range_array);

    if (result == NORMALIZE_RESULT_FAILURE) {
        success = FALSE;
    } else {
        success = TRUE;
    }

    if (success) {
        if (option_flags & PATCH_OPTION_SIGNATURE_MD5) {
            if (signature_bufsize >= (MD5DIGESTLEN*2+1)) {
                /* calculate MD5 hash of file buffer. */
                ComputeMD5Hash(file_buffer, (unsigned int)file_size, filehash);
                bin2hex(filehash, signature_buf, MD5DIGESTLEN);

            } else {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                success = FALSE;
            }

        } else {
            if (signature_bufsize >= (sizeof(UINT32)*2+1)) {
                /* calculate CRC32 checksum of file buffer. */
                filecrc = RtlComputeCrc32(0, file_buffer, file_size);
                dword2hex(filecrc, signature_buf);

            } else {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                success = FALSE;
            }
        }
    }

    if (!success) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_EXTENDED_ERROR);
        }
    }

    return success;
}

/*****************************************************
 *    NormalizeFileForPatchSignature (MSPATCHA.@)
 */
INT WINAPI NormalizeFileForPatchSignature(
    PVOID file_buffer, ULONG file_size,
    ULONG option_flags, PATCH_OPTION_DATA *option_data,
    ULONG new_coff_base, ULONG new_coff_time,
    ULONG ignore_range_count, PPATCH_IGNORE_RANGE ignore_range_array,
    ULONG retain_range_count, PPATCH_RETAIN_RANGE retain_range_array)
{
    return normalize_old_file_image(file_buffer, file_size,
                                    option_flags, option_data,
                                    new_coff_base, new_coff_time,
                                    ignore_range_array, ignore_range_count,
                                    retain_range_array, retain_range_count);
}
