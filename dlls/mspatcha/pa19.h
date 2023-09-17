/*
 * PatchAPI PA19 file format handlers
 *
 * Copyright 2019 Conor McCarthy
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

enum NORMALIZE_RESULT {
    NORMALIZE_RESULT_FAILURE = 0,
    NORMALIZE_RESULT_SUCCESS = 1,
    NORMALIZE_RESULT_SUCCESS_MODIFIED = 2
};

int normalize_old_file_image(
    BYTE *old_file_mapped, ULONG old_file_size,
    ULONG option_flags, PATCH_OPTION_DATA *option_data,
    ULONG new_image_base, ULONG new_image_time,
    const PATCH_IGNORE_RANGE *ignore_range_array, ULONG ignore_range_count,
    const PATCH_RETAIN_RANGE *retain_range_array, ULONG retain_range_count);

DWORD apply_patch_to_file_by_buffers(const BYTE *patch_file_view, const ULONG patch_file_size,
    const BYTE *old_file_view, ULONG old_file_size,
    BYTE **new_file_buf, const ULONG new_file_buf_size, ULONG *new_file_size,
    FILETIME *new_file_time,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only);

BOOL apply_patch_to_file_by_handles(HANDLE patch_file_hndl, HANDLE old_file_hndl, HANDLE new_file_hndl,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only);

BOOL apply_patch_to_file(LPCWSTR patch_file_name, LPCWSTR old_file_name, LPCWSTR new_file_name,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only);
