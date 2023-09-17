/*
 * PatchAPI PA19 file handlers
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
 *
 * TODO
 *  - Implement interleaved decoding when PATCH_OPTION_INTERLEAVE_FILES was
 *    used or the old file exceeds the lzxd window size.
 */

#include <stdarg.h>
#include <stdlib.h>

#include "windef.h"
#include "winternl.h"
#include "wine/heap.h"
#include "wine/debug.h"

#include "patchapi.h"

#include "pa19.h"
#include "lzxd_dec.h"

WINE_DEFAULT_DEBUG_CHANNEL(mspatcha);

#define PA19_FILE_MAGIC 0x39314150
#define PATCH_OPTION_EXTRA_FLAGS PATCH_OPTION_RESERVED1

/* patch header extra flags field values */
#define PATCH_TRANSFORM_NO_RELOCS      0x00000001
#define PATCH_TRANSFORM_NO_IMPORTS     0x00000002
#define PATCH_TRANSFORM_NO_EXPORTS     0x00000004
#define PATCH_TRANSFORM_NO_RELJMPS     0x00000008
#define PATCH_TRANSFORM_NO_RELCALLS    0x00000010
#define PATCH_TRANSFORM_NO_RESOURCE    0x00000020
#define PATCH_EXTRA_HAS_LZX_WINDOW_SIZE 0x00010000
#define PATCH_EXTRA_HAS_PATCH_TRANSFORMS 0x20000000

/***********************************************************************************
 *  PatchAPI PA19 file header
 *
 *  BYTE magic[4];
 *  UINT32 options;
 *  UINT32 options_2; (present if PATCH_OPTION_EXTRA_FLAGS set)
 *  UINT32 timestamp; (if PATCH_OPTION_NO_TIMESTAMP is SET in options)
 *  UVLI rebase;      (present if PATCH_OPTION_NO_REBASE is not set; used on 32-bit executables)
 *  UVLI unpatched_size;
 *  UINT32 crc32_patched;
 *  BYTE input_file_count;
 *
 *  For each source file:
 *      SVLI (patched_size - unpatched_size);
 *      UINT32 crc32_unpatched;
 *      BYTE ignore_range_count;
 *      For each ignore range:
 *          SVLI OffsetInOldFile;
 *          UVLI LengthInBytes;
 *      BYTE retain_range_count;
 *      For each retain range:
 *          SVLI (OffsetInOldFile - (prevOffsetInOldFile + prevLengthInBytes));
 *          SVLI (OffsetInNewFile - OffsetInOldFile);
 *          UVLI LengthInBytes;
 *      UVLI unknown_count; (a count of pairs of values related to the reversal of special
 *                           processing done to improve compression of 32-bit executables)
 *      UVLI interleave_count; (present only if PATCH_OPTION_INTERLEAVE_FILES is set in options)
 *          UVLI interleave_values[interleave_count * 3 - 1];
 *      UVLI lzxd_input_size;
 *
 *  For each source file:
 *      UINT16 lzxd_block[lzxd_input_size / 2]; (NOT always 16-bit aligned)
 *
 *  UINT32 crc_hack; (rounds out the entire file crc32 to 0)
*/

struct patch_transform_entry {
    ULONG old_rva;
    ULONG new_rva;
};
struct patch_transform_table {
    ULONG count;
    ULONG allocated;
    struct patch_transform_entry *entries;
    BYTE *unused;
};

struct old_file_info {
    HANDLE old_file_handle;                     /* 0x00 */
    ULONG old_size;                             /* 0x04 */
    ULONG old_crc32;                            /* 0x08 */
    ULONG patch_stream_size;                    /* 0x0C */
    ULONG ignore_range_count;                   /* 0x10 */
    PATCH_IGNORE_RANGE *ignore_range_array;     /* 0x14 */
    ULONG retain_range_count;                   /* 0x18 */
    PATCH_RETAIN_RANGE *retain_range_array;     /* 0x1C */
    struct patch_transform_table xfrm_tbl;      /* 0x20 */

    /* wine private file info fields */
    const BYTE *patch_stream_start;
};

struct patch_file_header {
    ULONG signature;                            /* 0x00 */
    ULONG flags;                                /* 0x04 */
    ULONG extra_flags;                          /* 0x08 */
    ULONG new_image_base;                       /* 0x0C */
    ULONG new_image_time;                       /* 0x10 */
    ULONG new_res_time;                         /* 0x14 */
    ULONG new_file_time;                        /* 0x18 */
    ULONG patched_size;                         /* 0x1C */
    ULONG patched_crc32;                        /* 0x20 */
    ULONG old_file_count;                       /* 0x24 */
    struct old_file_info *file_table;           /* 0x28 */
    PPATCH_INTERLEAVE_MAP *interleave_map_array;/* 0x2C */
    ULONG compression_window_size;              /* 0x30 */

    /* wine private patch header fields */
    const BYTE *src;
    const BYTE *end;
    ULONG err;
};

/* Currently supported options. Some such as PATCH_OPTION_FAIL_IF_BIGGER don't
 * affect decoding but can get recorded in the patch file anyway */
#define PATCH_OPTION_SUPPORTED_FLAGS ( \
      PATCH_OPTION_USE_LZX_A \
    | PATCH_OPTION_USE_LZX_B \
    | PATCH_OPTION_USE_LZX_LARGE \
    | PATCH_OPTION_NO_BINDFIX \
    | PATCH_OPTION_NO_LOCKFIX \
    | PATCH_OPTION_NO_REBASE \
    | PATCH_OPTION_FAIL_IF_SAME_FILE \
    | PATCH_OPTION_FAIL_IF_BIGGER \
    | PATCH_OPTION_NO_CHECKSUM \
    | PATCH_OPTION_NO_RESTIMEFIX \
    | PATCH_OPTION_NO_TIMESTAMP \
    | PATCH_OPTION_INTERLEAVE_FILES \
    | PATCH_OPTION_EXTRA_FLAGS)


/* read a byte-aligned little-endian UINT32 from input and set error if eof
 */
static inline UINT32 read_raw_uint32(struct patch_file_header *ph)
{
    const BYTE *src = ph->src;

    ph->src += 4;
    if (ph->src > ph->end)
    {
        ph->err = ERROR_PATCH_CORRUPT;
        return 0;
    }
    return src[0]
        | (src[1] << 8)
        | (src[2] << 16)
        | (src[3] << 24);
}

/* Read a variable-length integer from a sequence of bytes terminated by
 * a value with bit 7 set. Set error if invalid or eof */
static UINT64 read_uvli(struct patch_file_header *ph)
{
    const BYTE *vli = ph->src;
    UINT64 n;
    ptrdiff_t i;
    ptrdiff_t limit = min(ph->end - vli, 9);

    if (ph->src >= ph->end)
    {
        ph->err = ERROR_PATCH_CORRUPT;
        return 0;
    }

    n = vli[0] & 0x7F;
    for (i = 1; i < limit && vli[i - 1] < 0x80; ++i)
        n += (UINT64)(vli[i] & 0x7F) << (7 * i);

    if (vli[i - 1] < 0x80)
    {
        TRACE("exceeded maximum vli size\n");
        ph->err = ERROR_PATCH_CORRUPT;
        return 0;
    }

    ph->src += i;

    return n;
}

/* Signed variant of the above. First byte sign flag is 0x40.
 */
static INT64 read_svli(struct patch_file_header *ph)
{
    const BYTE *vli = ph->src;
    INT64 n;
    ptrdiff_t i;
    ptrdiff_t limit = min(ph->end - vli, 9);

    if (ph->src >= ph->end)
    {
        ph->err = ERROR_PATCH_CORRUPT;
        return 0;
    }

    n = vli[0] & 0x3F;
    for (i = 1; i < limit && vli[i - 1] < 0x80; ++i)
        n += (INT64)(vli[i] & 0x7F) << (7 * i - 1);

    if (vli[i - 1] < 0x80)
    {
        TRACE("exceeded maximum vli size\n");
        ph->err = ERROR_PATCH_CORRUPT;
        return 0;
    }

    if (vli[0] & 0x40)
        n = -n;

    ph->src += i;

    return n;
}

static int __cdecl compare_ignored_range(const void *a, const void *b)
{
    const LONG delta = ((const PATCH_IGNORE_RANGE*)a)->OffsetInOldFile
                        - ((const PATCH_IGNORE_RANGE*)b)->OffsetInOldFile;
    if (delta > 0) return 1;
    else if (delta < 0) return -1;
    else return 0;
}

static int __cdecl compare_retained_range_old(const void *a, const void *b)
{
    const LONG delta = ((const PATCH_RETAIN_RANGE*)a)->OffsetInOldFile
                        - ((const PATCH_RETAIN_RANGE*)b)->OffsetInOldFile;
    if (delta > 0) return 1;
    else if (delta < 0) return -1;
    else return 0;
}

static int read_patch_header(struct patch_file_header *ph, const BYTE *buf, size_t size)
{
    ULONG signature;
    ULONG fileno;
    struct old_file_info *fi;
    ULONG i;
    ULONG window_shift;
    LONG delta;
    LONG delta_new;
    ULONG delta_old;
    LONG delta_next;
    LONG delta_length;
    LONG length_next;
    ULONG next_offset;
    ULONG old_rva;
    ULONG new_rva;
    ULONG length;
    ULONG next_old_offset;
    ULONG total_old_offset;
    ULONG next_old_length;
    ULONG total_old_length;
    ULONG remaining_length;
    ULONG new_length;
    ULONG interleave_count;
    PATCH_INTERLEAVE_MAP *interleave_map;

    ph->src = buf;
    ph->end = buf + size;

    ph->file_table = NULL;
    ph->err = ERROR_SUCCESS;

    signature = read_raw_uint32(ph);
    ph->signature = signature;
    if (signature != PA19_FILE_MAGIC)
    {
        WARN("no PA19 signature\n");
        ph->err = ERROR_PATCH_CORRUPT;
        return -1;
    }

    ph->flags = read_raw_uint32(ph);

    /* the meaning of PATCH_OPTION_NO_TIMESTAMP is inverted for decoding */
    ph->flags ^= PATCH_OPTION_NO_TIMESTAMP;

    if ((ph->flags & PATCH_OPTION_SUPPORTED_FLAGS) != ph->flags)
    {
        FIXME("unsupported option flag(s): 0x%08lx\n", ph->flags & ~PATCH_OPTION_SUPPORTED_FLAGS);
        ph->err = ERROR_PATCH_PACKAGE_UNSUPPORTED;
        return -1;
    }

    /* extra 32-bit flags field */
    if (ph->flags & PATCH_OPTION_EXTRA_FLAGS)
    {
        ph->extra_flags = read_raw_uint32(ph);
        WARN("extra flags field is set: 0x%lX\n", ph->extra_flags);
    }

    /* decode new file time field */
    if ((ph->flags & PATCH_OPTION_NO_TIMESTAMP) == 0)
    {
        ph->new_file_time = read_raw_uint32(ph);
    }

    /* read the new coff image base and time used for normalization */
    if ((ph->flags & PATCH_OPTION_NO_REBASE) == 0)
    {
        ph->new_image_base = ((ULONG)*(UINT16 UNALIGNED *)ph->src) << 16;
        ph->src += sizeof(UINT16);

        TRACE("new file requires rebasing to 0x%lX\n", ph->new_image_base);

        if (ph->new_file_time) {
            delta = read_svli(ph);
            ph->new_image_time = ph->new_file_time - delta;
        } else {
            ph->new_image_time = read_raw_uint32(ph);
        }
    }

    if ((ph->flags & PATCH_OPTION_NO_RESTIMEFIX) == 0)
    {
        if (ph->new_image_time) {
            delta = read_svli(ph);
            ph->new_res_time = ph->new_image_time - delta;
        } else {
            ph->new_res_time = read_raw_uint32(ph);
        }
    }

    ph->patched_size = read_uvli(ph);
    TRACE("patched file size will be %lu\n", (ULONG)ph->patched_size);
    ph->patched_crc32 = read_raw_uint32(ph);

    if (ph->extra_flags & PATCH_EXTRA_HAS_LZX_WINDOW_SIZE)
    {
        window_shift = (ULONG)*ph->src;
        ++ph->src;

        if (window_shift > 31)  {
            ERR("invalid compression window shift!\n");
            ph->err = ERROR_EXTENDED_ERROR;
            return -1;
        }

        ph->compression_window_size = (ULONG)1ul << window_shift;
        TRACE("compression window size: %lu\n", ph->compression_window_size);
    }

    ph->old_file_count = (ULONG)*ph->src;
    ++ph->src;
    TRACE("patch supports %lu old file(s)\n", ph->old_file_count);
    /* if no old file used, old_file_count is still 1 */
    if (ph->old_file_count == 0) {
        ph->err = ERROR_PATCH_CORRUPT;
        return -1;
    }

    if (ph->err != ERROR_SUCCESS) {
        return -1;
    }

    ph->file_table = heap_calloc(ph->old_file_count, sizeof(struct old_file_info));
    if (ph->file_table == NULL)
    {
        ph->err = ERROR_OUTOFMEMORY;
        return -1;
    }

    /* If the interleave files option is set, allocate memory for the interleave map */
    if (ph->flags & PATCH_OPTION_INTERLEAVE_FILES)
    {
        ph->interleave_map_array = heap_calloc(ph->old_file_count, sizeof(PPATCH_INTERLEAVE_MAP));
        if (ph->interleave_map_array == NULL) {
            ph->err = ERROR_OUTOFMEMORY;
            return -1;
        }
    }

    for (fileno = 0; fileno < ph->old_file_count; ++fileno)
    {
        fi = &ph->file_table[fileno];

        delta = read_svli(ph);
        fi->old_size = ph->patched_size + delta;

        fi->old_crc32 = read_raw_uint32(ph);

        /* decode ignore range table */
        fi->ignore_range_count = *ph->src;
        ++ph->src;
        if (fi->ignore_range_count)
        {
            TRACE("found %lu range(s) to ignore\n", fi->ignore_range_count);

            fi->ignore_range_array = heap_calloc(fi->ignore_range_count, sizeof(PATCH_IGNORE_RANGE));
            if (fi->ignore_range_array == NULL) {
                ph->err = ERROR_OUTOFMEMORY;
                return -1;
            }

            next_offset = 0;
            for (i = 0; i < fi->ignore_range_count; ++i)
            {
                delta = (LONG)read_svli(ph);
                length = (ULONG)read_uvli(ph);

                fi->ignore_range_array[i].OffsetInOldFile = next_offset + delta;
                fi->ignore_range_array[i].LengthInBytes = length;

                if (fi->ignore_range_array[i].OffsetInOldFile > fi->old_size
                    || (fi->ignore_range_array[i].OffsetInOldFile + length) > fi->old_size)
                {
                    ph->err = ERROR_PATCH_CORRUPT;
                    return -1;
                }

                next_offset += delta + length;
            }
        }

        /* decode retain range table */
        fi->retain_range_count = *ph->src;
        ++ph->src;
        if (fi->retain_range_count)
        {
            TRACE("found %lu range(s) to retain\n", fi->retain_range_count);

            fi->retain_range_array = heap_calloc(fi->retain_range_count, sizeof(PATCH_RETAIN_RANGE));
            if (fi->retain_range_array == NULL)
            {
                ph->err = ERROR_OUTOFMEMORY;
                return -1;
            }

            next_offset = 0;
            for (i = 0; i < fi->retain_range_count; ++i)
            {
                delta = (LONG)read_svli(ph);
                delta_new = (LONG)read_svli(ph);
                length = (ULONG)read_uvli(ph);

                fi->retain_range_array[i].OffsetInOldFile = next_offset + delta;
                fi->retain_range_array[i].OffsetInNewFile = next_offset + delta + delta_new;
                fi->retain_range_array[i].LengthInBytes = length;

                if (fi->retain_range_array[i].OffsetInOldFile > fi->old_size
                    || (fi->retain_range_array[i].OffsetInOldFile + fi->retain_range_array[i].LengthInBytes) > fi->old_size
                    || fi->retain_range_array[i].OffsetInNewFile > ph->patched_size
                    || (fi->retain_range_array[i].OffsetInNewFile + fi->retain_range_array[i].LengthInBytes) > ph->patched_size)
                {
                    ph->err = ERROR_PATCH_CORRUPT;
                    return -1;
                }

                /* ranges in new file must be equal and in the same order for all source files */
                if (fileno
                    && (fi->retain_range_array[i].OffsetInNewFile != ph->file_table[0].retain_range_array[i].OffsetInNewFile
                        || fi->retain_range_array[i].LengthInBytes != ph->file_table[0].retain_range_array[i].LengthInBytes))
                {
                    ph->err = ERROR_PATCH_CORRUPT;
                    return -1;
                }

                next_offset += delta + length;
            }
        }

        /* decode patch transform table */
        fi->xfrm_tbl.count = (ULONG)read_uvli(ph);
        if (fi->xfrm_tbl.count)
        {
            TRACE("%lu patch transform entries found\n", fi->xfrm_tbl.count);

            fi->xfrm_tbl.entries = heap_calloc(fi->xfrm_tbl.count, sizeof(struct patch_transform_entry));
            if (fi->xfrm_tbl.entries == NULL) {
                ph->err = ERROR_OUTOFMEMORY;
                return -1;
            }

            fi->xfrm_tbl.unused = NULL;

            old_rva = 0;
            new_rva = 0;
            for (i = 0; i < fi->xfrm_tbl.count; i++)
            {
                delta_old = read_uvli(ph);
                delta_new = read_svli(ph);

                fi->xfrm_tbl.entries[i].old_rva = old_rva + delta_old;
                fi->xfrm_tbl.entries[i].new_rva = new_rva + delta_new;

                old_rva += delta_old;
                new_rva += delta_new;
            }
        }

        /* decode interleave map */
        if (ph->flags & PATCH_OPTION_INTERLEAVE_FILES)
        {
            interleave_count = (ULONG)read_uvli(ph);
            if (interleave_count)
            {
                interleave_map = (PATCH_INTERLEAVE_MAP *)heap_alloc(
                    sizeof(((PATCH_INTERLEAVE_MAP *)0)->Range) * (interleave_count - 1)
                            + sizeof(PATCH_INTERLEAVE_MAP));

                if (interleave_map == NULL)
                {
                    ph->err = ERROR_OUTOFMEMORY;
                    return -1;
                }

                interleave_map->CountRanges = interleave_count;

                total_old_offset = 0;
                next_old_offset = 0;
                total_old_length = 0;
                next_old_length = 0;
                new_length = 0;
                remaining_length = ph->patched_size;

                for (i = 0; i < interleave_map->CountRanges; i++)
                {
                    delta_next = (LONG)read_svli(ph);
                    length_next = (LONG)read_svli(ph);

                    total_old_offset += delta_next;
                    total_old_length += length_next;

                    next_old_offset += total_old_offset;
                    next_old_length += total_old_length;

                    interleave_map->Range[i].OldOffset = next_old_offset;
                    interleave_map->Range[i].OldLength = next_old_length;

                    if (interleave_map->Range[i].OldOffset > fi->old_size
                        || (interleave_map->Range[i].OldOffset
                            + interleave_map->Range[i].OldLength) > fi->old_size)
                    {
                        ph->err = ERROR_PATCH_CORRUPT;
                        return -1;
                    }

                    if (i >= (interleave_map->CountRanges - 1))
                    {
                        interleave_map->Range[i].NewLength = remaining_length;
                    }
                    else
                    {
                        delta_length = (LONG)read_svli(ph) << 15;
                        new_length += delta_length;
                        interleave_map->Range[i].NewLength = new_length;
                        remaining_length -= new_length;
                    }
                }

                ph->interleave_map_array[fileno] = interleave_map;
            }
        }

        fi->patch_stream_size = (ULONG)read_uvli(ph);
    }

    /* sort range tables and calculate patch stream start for each file */
    for (fileno = 0; fileno < ph->old_file_count; ++fileno)
    {
        fi = &ph->file_table[fileno];
        qsort(fi->ignore_range_array, fi->ignore_range_count, sizeof(fi->ignore_range_array[0]), compare_ignored_range);
        qsort(fi->retain_range_array, fi->retain_range_count, sizeof(fi->retain_range_array[0]), compare_retained_range_old);

        fi->patch_stream_start = ph->src;
        ph->src += fi->patch_stream_size;
    }

    /* skip the crc adjustment field */
    ph->src = min(ph->src + 4, ph->end);

    {
        if (RtlComputeCrc32(0, buf, ph->src - buf) ^ 0xFFFFFFFF)
        {
            TRACE("patch file crc32 failed\n");
            if (ph->src < ph->end)
                FIXME("probable header parsing error\n");
            ph->err = ERROR_PATCH_CORRUPT;
        }
    }

    return (ph->err == ERROR_SUCCESS) ? 0 : -1;
}

static void free_header(struct patch_file_header *ph)
{
    ULONG i;

    if (ph->interleave_map_array) {
        for (i = 0; i < ph->old_file_count; i++) {
            if (ph->interleave_map_array[i]) {
                heap_free(ph->interleave_map_array[i]);
                ph->interleave_map_array[i] = NULL;
            }
        }

        heap_free(ph->interleave_map_array);
        ph->interleave_map_array = NULL;
    }

    if (ph->file_table) {
        for (i = 0; i < ph->old_file_count; i++) {
            if (ph->file_table[i].ignore_range_array) {
                heap_free(ph->file_table[i].ignore_range_array);
                ph->file_table[i].ignore_range_array = NULL;
            }
            if (ph->file_table[i].retain_range_array) {
                heap_free(ph->file_table[i].retain_range_array);
                ph->file_table[i].retain_range_array = NULL;
            }
        }

        heap_free(ph->file_table);
        ph->file_table = NULL;
    }
}

#define TICKS_PER_SEC 10000000
#define SEC_TO_UNIX_EPOCH ((369 * 365 + 89) * (ULONGLONG)86400)

static void posix_time_to_file_time(ULONG timestamp, FILETIME *ft)
{
    UINT64 ticks = ((UINT64)timestamp + SEC_TO_UNIX_EPOCH) * TICKS_PER_SEC;
    ft->dwLowDateTime = (DWORD)ticks;
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
}

/* Save the retain ranges into an allocated buffer.
 */
static PUCHAR save_retained_ranges(
    const BYTE *file_buf, ULONG file_size,
    const PATCH_RETAIN_RANGE *retain_range_array, ULONG retain_range_count,
    BOOL from_new)
{
    PUCHAR buffer, ptr;
    ULONG i;
    ULONG offset_in_file;
    ULONG allocation_size;

    allocation_size = 0;
    for (i = 0; i < retain_range_count; i++) {
        allocation_size += retain_range_array[i].LengthInBytes;
    }

    buffer = VirtualAlloc(NULL, allocation_size, MEM_COMMIT, PAGE_READWRITE);
    if (buffer) {
        ptr = buffer;
        for (i = 0; i < retain_range_count; i++) {
            if (from_new) {
                offset_in_file = retain_range_array[i].OffsetInNewFile;
            } else {
                offset_in_file = retain_range_array[i].OffsetInOldFile;
            }

            if (offset_in_file <= file_size
                && (offset_in_file + retain_range_array[i].LengthInBytes) <= file_size)
            {
                memcpy(ptr, &file_buf[offset_in_file], retain_range_array[i].LengthInBytes);
            }

            ptr += retain_range_array[i].LengthInBytes;
        }
    }

    return buffer;
}

/* Copy the saved retained ranges to the new file buffer
 */
static void apply_retained_ranges(
    BYTE *new_file_buf, const PATCH_RETAIN_RANGE *retain_range_array,
    const ULONG retain_range_count, const BYTE *retain_buffer)
{
    if (retain_buffer == NULL)
        return;

    for (ULONG i = 0; i < retain_range_count; ++i)
    {
        memcpy(new_file_buf + retain_range_array[i].OffsetInNewFile,
               retain_buffer,
               retain_range_array[i].LengthInBytes);

        retain_buffer += retain_range_array[i].LengthInBytes;
    }
}

static void DECLSPEC_NORETURN throw_pe_fmt_exception(void)
{
    RaiseException(0xE0000001, 0, 0, NULL);
    for (;;) { /* silence compiler warning */ }
}

static IMAGE_NT_HEADERS32 UNALIGNED *image_get_nt_headers(const void *image_base, size_t image_size)
{
    IMAGE_DOS_HEADER UNALIGNED *dos_hdr;
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers;
    const UCHAR *const image_end = (PUCHAR)image_base + image_size;

    if (image_size >= 0x200) {
        dos_hdr = (IMAGE_DOS_HEADER *)image_base;
        if (dos_hdr->e_magic == IMAGE_DOS_SIGNATURE && dos_hdr->e_lfanew < image_size) {
            nt_headers = (IMAGE_NT_HEADERS32 *)((ULONG_PTR)dos_hdr + dos_hdr->e_lfanew);
            if (((PUCHAR)nt_headers + sizeof(IMAGE_NT_HEADERS32)) <= image_end) {
                if (nt_headers->Signature == IMAGE_NT_SIGNATURE
                    && nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                    return nt_headers;
                }
            }
        }
    }

    return NULL;
}

static ULONG image_rva_to_file_offset(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, ULONG rva, PUCHAR image_base, ULONG image_size)
{
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG section_count;
    ULONG i;

    if ( rva < nt_headers->OptionalHeader.SizeOfHeaders ) {
        return rva;
    }

    section_table = IMAGE_FIRST_SECTION(nt_headers);
    section_count = nt_headers->FileHeader.NumberOfSections;
    for (i = 0; i < section_count; i++) {
        if ((PUCHAR)&section_table[i] < image_base
            || ((PUCHAR)&section_table[i] + sizeof(IMAGE_SECTION_HEADER)) > &image_base[image_size]) {
            throw_pe_fmt_exception();
        }

        if (rva >= section_table[i].VirtualAddress
            && rva < (section_table[i].VirtualAddress + section_table[i].SizeOfRawData)) {
            return (section_table[i].PointerToRawData + (rva - section_table[i].VirtualAddress));
        }
    }

    return 0;
}

static ULONG image_directory_rva_and_size(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, USHORT directory_entry, ULONG *directory_size,
    PUCHAR image_base, ULONG image_size)
{
    IMAGE_DATA_DIRECTORY UNALIGNED *data_directory;

    if (directory_entry >= nt_headers->OptionalHeader.NumberOfRvaAndSizes) {
        return 0;
    }

    data_directory = &nt_headers->OptionalHeader.DataDirectory[directory_entry];
    if ((PUCHAR)data_directory < image_base
        || ((PUCHAR)data_directory + sizeof(IMAGE_DATA_DIRECTORY)) > &image_base[image_size]) {
        throw_pe_fmt_exception();
    }

    if (directory_size != NULL) {
        *directory_size = data_directory->Size;
    }
    return data_directory->VirtualAddress;
}

static ULONG image_directory_offset_and_size(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, USHORT directory_entry, ULONG *directory_size,
    PUCHAR image_base, ULONG image_size)
{
    ULONG rva, offset = 0;
    rva = image_directory_rva_and_size(nt_headers, directory_entry, directory_size, image_base, image_size);
    if (rva) {
        offset = image_rva_to_file_offset(nt_headers, rva, image_base, image_size);
    }
    return offset;
}

static PVOID image_rva_to_mapped_address(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, ULONG rva, PVOID image_base, ULONG image_size)
{
    const ULONG offset = image_rva_to_file_offset(nt_headers, rva, image_base, image_size);
    if (offset && offset < image_size) {
        return (PVOID)((PUCHAR)image_base + offset);
    }
    return NULL;
}

static PVOID image_directory_mapped_address(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, USHORT directory_entry, ULONG *directory_size,
    PUCHAR image_base, ULONG image_size)
{
    ULONG dir_rva;
    ULONG dir_size;
    PVOID mapped_address;

    dir_rva = image_directory_rva_and_size(nt_headers, directory_entry, &dir_size, image_base, image_size);
    if (!dir_rva) {
        return NULL;
    }

    mapped_address = image_rva_to_mapped_address(nt_headers, dir_rva, image_base, image_size);
    if (!mapped_address) {
        return NULL;
    }

    if (((PUCHAR)mapped_address + dir_size) < (PUCHAR)mapped_address) {
        throw_pe_fmt_exception();
    }

    if (((PUCHAR)mapped_address + dir_size) > &image_base[image_size]) {
        return NULL;
    }

    if (directory_size != NULL) {
        *directory_size = dir_size;
    }

    return mapped_address;
}

/* Fixup a given mapped image's relocation table for a new image base. */
static BOOL rebase_image(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers,
    PUCHAR mapped_image_base, ULONG mapped_image_size, ULONG new_image_base)
{
    BOOL result;
    IMAGE_BASE_RELOCATION UNALIGNED *reloc_block;
    IMAGE_BASE_RELOCATION UNALIGNED *reloc_block_base;
    PUCHAR reloc_fixup;
    LONG delta;
    LONG reloc_dir_remaining;
    ULONG reloc_dir_size;
    USHORT UNALIGNED *reloc;
    ULONG reloc_count;
    PUCHAR mapped_image_end;

    result = FALSE;
    mapped_image_end = &mapped_image_base[mapped_image_size];
    delta = (LONG)(new_image_base - nt_headers->OptionalHeader.ImageBase);

    reloc_dir_size = 0;
    reloc_block = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_BASERELOC,
                                        &reloc_dir_size, mapped_image_base, mapped_image_size);
    if (!reloc_block
        || !reloc_dir_size
        || ((PUCHAR)reloc_block + sizeof(IMAGE_BASE_RELOCATION)) > mapped_image_end) {
        return result;
    }

    nt_headers->OptionalHeader.ImageBase = (DWORD)new_image_base;
    result = TRUE;

    reloc_dir_remaining = (LONG)reloc_dir_size;
    while (reloc_dir_remaining > 0
        && reloc_block->SizeOfBlock <= (ULONG)reloc_dir_remaining
        && reloc_block->SizeOfBlock > sizeof(IMAGE_BASE_RELOCATION))
    {
        reloc_block_base = (IMAGE_BASE_RELOCATION UNALIGNED *)(mapped_image_base +
            image_rva_to_file_offset(nt_headers, reloc_block->VirtualAddress, mapped_image_base, mapped_image_size));
        if (reloc_block_base)
        {
            reloc = (USHORT UNALIGNED *)((PUCHAR)reloc_block + sizeof(IMAGE_BASE_RELOCATION));
            reloc_count = (reloc_block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
            while (reloc_count--) {
                if ((PUCHAR)reloc > mapped_image_end) {
                    break;
                }

                reloc_fixup = (PUCHAR)reloc_block_base + (*reloc & 0x0FFF);
                if (reloc_fixup < mapped_image_end)
                {
                    switch (*reloc >> 12)
                    {
                    case IMAGE_REL_BASED_HIGH:
                        *(USHORT UNALIGNED *)reloc_fixup = (USHORT)(((*(USHORT UNALIGNED *)reloc_fixup << 16) + delta) >> 16);
                        break;
                    case IMAGE_REL_BASED_LOW:
                        *(USHORT UNALIGNED *)reloc_fixup = (USHORT)(*(SHORT UNALIGNED *)reloc_fixup + delta);
                        break;
                    case IMAGE_REL_BASED_HIGHLOW:
                        if (reloc_fixup + sizeof(USHORT) <= mapped_image_end) {
                            *(LONG UNALIGNED *)reloc_fixup += delta;
                        }
                        break;
                    case IMAGE_REL_BASED_HIGHADJ:
                        ++reloc;
                        --reloc_count;
                        if ((PUCHAR)reloc <= (mapped_image_end - sizeof(USHORT)))
                            *(USHORT UNALIGNED *)reloc_fixup = (USHORT)(((*(USHORT UNALIGNED *)reloc_fixup << 16) + (SHORT)*reloc + delta + 0x8000) >> 16);
                        break;
                    default:

                    }
                }

                ++reloc;
            }
        }

        reloc_dir_remaining -= reloc_block->SizeOfBlock;
        reloc_block = (IMAGE_BASE_RELOCATION UNALIGNED *)((PUCHAR)reloc_block + reloc_block->SizeOfBlock);

        if ((PUCHAR)reloc_block > (mapped_image_end - sizeof(IMAGE_BASE_RELOCATION))) {
            throw_pe_fmt_exception();
        }
    }

    return result;
}

/* Remove all bound imports for a given mapped image. */
static BOOL unbind_image(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, ULONG image_size)
{
    BOOL result;
    PUCHAR mapped_image_end;
    IMAGE_IMPORT_DESCRIPTOR UNALIGNED *import_desc;
    IMAGE_BOUND_IMPORT_DESCRIPTOR UNALIGNED *bound_imports;
    ULONG bound_imports_size;
    IMAGE_DATA_DIRECTORY UNALIGNED *bound_import_data_dir;
    IMAGE_THUNK_DATA32 UNALIGNED *original_thunk;
    IMAGE_THUNK_DATA32 UNALIGNED *bound_thunk;
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG section_count;

    result = FALSE;
    mapped_image_end = mapped_image + image_size;

    /* Erase bound import data directory. */
    bound_imports = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT,
                                                &bound_imports_size, mapped_image, image_size);
    if (bound_imports)
    {
        memset(bound_imports, 0, bound_imports_size);

        bound_import_data_dir = &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT];
        if ((PUCHAR)bound_import_data_dir < mapped_image
            || (PUCHAR)bound_import_data_dir > (mapped_image_end - sizeof(IMAGE_DATA_DIRECTORY))) {
            throw_pe_fmt_exception();
        }

        bound_import_data_dir->VirtualAddress = 0;
        bound_import_data_dir->Size = 0;
        result = TRUE;
    }

    /* Zero out needed import descriptor fields */
    import_desc = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_IMPORT, NULL, mapped_image, image_size);
    if (import_desc)
    {
        while ((PUCHAR)import_desc < (mapped_image_end - sizeof(IMAGE_IMPORT_DESCRIPTOR))
                && import_desc->Characteristics)
        {
            /* TimeDateStamp field is -1 if bound */
            if (import_desc->TimeDateStamp) {
                import_desc->TimeDateStamp = 0;
                result = TRUE;

                original_thunk = image_rva_to_mapped_address(nt_headers, import_desc->OriginalFirstThunk, mapped_image, image_size);
                bound_thunk = image_rva_to_mapped_address(nt_headers, import_desc->FirstThunk, mapped_image, image_size);
                if (original_thunk && bound_thunk)
                {
                    for (; original_thunk->u1.AddressOfData; original_thunk++, bound_thunk++)
                    {
                        if ((PUCHAR)original_thunk >= mapped_image_end
                            || (PUCHAR)bound_thunk >= mapped_image_end)
                            break;
                        *bound_thunk = *original_thunk;
                    }
                }
            }

            if (import_desc->ForwarderChain) {
                import_desc->ForwarderChain = 0;
                result = TRUE;
            }

            ++import_desc;
        }
    }

    /* Mark the .idata section as writable */
    section_table = IMAGE_FIRST_SECTION(nt_headers);
    section_count = nt_headers->FileHeader.NumberOfSections;
    for (ULONG i = 0; i < section_count; i++)
    {
        if ((PUCHAR)&section_table[i] < mapped_image
            || (PUCHAR)&section_table[i] > (mapped_image_end - sizeof(IMAGE_SECTION_HEADER))) {
            throw_pe_fmt_exception();
        }

        /* check for ".idata  " */
        if (strnicmp((char *)section_table[i].Name, ".idata  ", IMAGE_SIZEOF_SHORT_NAME) == 0) {
            if ((section_table[i].Characteristics & IMAGE_SCN_MEM_WRITE) == 0) {
                result = TRUE;
                section_table[i].Characteristics |= (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
            }

            break;
        }
    }

    return result;
}

/* Force all lock prefixes to the x86 LOCK (F0h) opcode in a given mapped image. */
static BOOL normalize_lock_prefixes_in_image(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, ULONG image_size)
{
    BOOL result = FALSE;
    PUCHAR mapped_image_end;
    IMAGE_LOAD_CONFIG_DIRECTORY32 UNALIGNED *loadcfg;
    ULONG UNALIGNED *lock_prefix_table;

    mapped_image_end = &mapped_image[image_size];

    loadcfg = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                            NULL, mapped_image, image_size);
    if (loadcfg && loadcfg->LockPrefixTable)
    {
        if (loadcfg->LockPrefixTable < nt_headers->OptionalHeader.ImageBase) {
            throw_pe_fmt_exception();
        }

        lock_prefix_table = image_rva_to_mapped_address(nt_headers,
            loadcfg->LockPrefixTable - nt_headers->OptionalHeader.ImageBase, mapped_image, image_size);

        if (lock_prefix_table)
        {
            while ((PUCHAR)lock_prefix_table <= (mapped_image_end - sizeof(ULONG))
                    && *lock_prefix_table)
            {
                PUCHAR const p = image_rva_to_mapped_address(nt_headers,
                    *lock_prefix_table - nt_headers->OptionalHeader.ImageBase, mapped_image, image_size);

                if (p && *p != 0xF0) {
                    *p = 0xF0;
                    result = TRUE;
                }

                ++lock_prefix_table;
            }
        }
    }

    return result;
}

/* derived from imagehlp for calculating a new coff image checksum. */
static WORD calc_chksum(
    ULONG initial_value, PUCHAR buffer, ULONG size_in_bytes)
{
    ULONG sum;
    ULONG i;
    ULONG word_count;
    PUCHAR p;

    sum = initial_value;
    p = buffer;
    word_count = size_in_bytes / sizeof(WORD);
    for (i = 0; i < word_count; i++)
    {
        sum += *(WORD *)p;
        if (HIWORD(sum) != 0) {
            sum = LOWORD(sum) + HIWORD(sum);
        }
        p += sizeof(WORD);
    }

    if (size_in_bytes & 1) {
        sum += *p;
    }

    return (WORD)(HIWORD(sum) + LOWORD(sum));
}

/* Normalizes a given 32-bit PE image to render a stream that is common. */
int normalize_old_file_image(
    BYTE *old_file_buffer, ULONG old_file_size,
    ULONG option_flags,  PATCH_OPTION_DATA *option_data,
    ULONG new_image_base, ULONG new_image_time,
    const PATCH_IGNORE_RANGE *ignore_range_array, ULONG ignore_range_count,
    const PATCH_RETAIN_RANGE *retain_range_array, ULONG retain_range_count)
{
    BOOL modified = FALSE;
    ULONG i;
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers;

    TRACE("normalizing image at 0x%p with options 0x%lX, new base 0x%lX, new time %lu\n",
        old_file_buffer, option_flags, new_image_base, new_image_time);

    if (!old_file_buffer || !old_file_size) {
        return NORMALIZE_RESULT_SUCCESS;
    }

    nt_headers = image_get_nt_headers(old_file_buffer, old_file_size);
    if (nt_headers)
    {
        if ((option_flags & PATCH_OPTION_NO_REBASE) == 0) {
            if (new_image_time != 0 && nt_headers->FileHeader.TimeDateStamp != new_image_time) {
                nt_headers->FileHeader.TimeDateStamp = new_image_time;
                modified = TRUE;
            }

            if (new_image_base != 0 && nt_headers->OptionalHeader.ImageBase != new_image_base) {
                modified |= rebase_image(nt_headers, old_file_buffer, old_file_size, new_image_base);
            }
        }

        if ((option_flags & PATCH_OPTION_NO_BINDFIX) == 0) {
            modified |= unbind_image(nt_headers, old_file_buffer, old_file_size);
        }

        if ((option_flags & PATCH_OPTION_NO_LOCKFIX) == 0) {
            modified |= normalize_lock_prefixes_in_image(nt_headers, old_file_buffer, old_file_size);
        }

        if ((option_flags & PATCH_OPTION_NO_CHECKSUM) != 0) {
            if (nt_headers->OptionalHeader.CheckSum) {
                nt_headers->OptionalHeader.CheckSum = 0;
                modified = TRUE;
            }

        } else {
            if (modified) {
                nt_headers->OptionalHeader.CheckSum = 0;
                nt_headers->OptionalHeader.CheckSum = calc_chksum(0, old_file_buffer, old_file_size) + old_file_size;
            }
        }
    }

    for (i = 0; i < ignore_range_count; ++i) {
        if (ignore_range_array[i].OffsetInOldFile <= old_file_size
            && (ignore_range_array[i].OffsetInOldFile + ignore_range_array[i].LengthInBytes) <= old_file_size)
        {
            memset(&old_file_buffer[ignore_range_array[i].OffsetInOldFile],
                   0,
                   ignore_range_array[i].LengthInBytes);
            modified = TRUE;
        }
    }

    for (i = 0; i < retain_range_count; ++i) {
        if (retain_range_array[i].OffsetInOldFile <= old_file_size
            && (retain_range_array[i].OffsetInOldFile + retain_range_array[i].LengthInBytes) <= old_file_size)
        {
            memset(&old_file_buffer[retain_range_array[i].OffsetInOldFile],
                   0,
                   retain_range_array[i].LengthInBytes);
            modified = TRUE;
        }
    }

    return modified ? NORMALIZE_RESULT_SUCCESS_MODIFIED : NORMALIZE_RESULT_SUCCESS;
}

static void patch_transform_PE_mark_non_executable(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, ULONG image_size,
    BYTE *hintmap, BOOL force)
{
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG section_size_raw;
    ULONG directory_offset;
    ULONG directory_rva;
    ULONG directory_size;
    ULONG i;

    /* Mark all non-executable sections in the hint map */
    section_table = IMAGE_FIRST_SECTION(nt_headers);
    for (i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
        if ((section_table[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0
            && section_table[i].PointerToRawData < image_size) {
            section_size_raw = section_table[i].SizeOfRawData;
            if (section_size_raw > (image_size - section_table[i].PointerToRawData))
                section_size_raw = image_size - section_table[i].PointerToRawData;
            memset(&hintmap[section_table[i].PointerToRawData], 0x01, section_size_raw);
        }
    } 

    /* Mark headers in the hint map */
    if (nt_headers->OptionalHeader.SizeOfHeaders > image_size) {
        nt_headers->OptionalHeader.SizeOfHeaders = image_size;
    }
    memset(hintmap, 0x03, nt_headers->OptionalHeader.SizeOfHeaders);

    /* Mark all other non-executable regions in the hint map */
    for (i = 0; i < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; i++) {
        directory_rva = nt_headers->OptionalHeader.DataDirectory[i].VirtualAddress;
        directory_size = nt_headers->OptionalHeader.DataDirectory[i].Size;
        if (directory_rva && directory_size) {
            directory_offset = image_rva_to_file_offset(nt_headers, directory_rva, mapped_image, image_size);
            if ((directory_offset || force) && directory_offset < image_size) {
                if (directory_size > image_size - directory_offset) {
                    directory_size = image_size - directory_offset;
                }
                memset(&hintmap[directory_offset], 0x03, directory_size);
            }
        }
    }
}

static ULONG get_new_rva_from_xfrm_table(struct patch_transform_table *xfrm_tbl, ULONG old_rva)
{
    ULONG mid, low = 0, high = xfrm_tbl->count;
    struct patch_transform_entry *const entries = xfrm_tbl->entries;

    /* Use a binary search to locate the new rva. */
    while (low < high) {
        mid = (low + high) >> 1;
        if (entries[mid].old_rva >= old_rva) {
            if (entries[mid].old_rva == old_rva) {
                return old_rva + (LONG)(entries[mid].new_rva - entries[mid].old_rva);
            }
            high = mid;
        } else {
            low = mid + 1;
        }
    }

    if (!low) {
        return old_rva;
    }

    mid = low - 1;
    return old_rva + (LONG)(entries[mid].new_rva - entries[mid].old_rva);
}

struct reloc_entry {
    ULONG rva;
    UCHAR type;
    USHORT hiadj;
};

static int __cdecl reloc_entry_compare(const void *a, const void *b)
{
    const LONG diff = ((const struct reloc_entry *)a)->rva
                        - ((const struct reloc_entry *)b)->rva;
    if (diff > 0) return 1;
    else if (diff < 0) return -1;
    else return 0;
}

static void patch_transform_PE_relocations(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, ULONG image_size,
    struct patch_transform_table *xfrm_tbl, BYTE *hintmap)
{
    PUCHAR mapped_image_end;
    ULONG image_base_va;
    ULONG reloc_dir_offset;
    ULONG reloc_dir_size;
    LONG reloc_dir_remaining;
    ULONG reloc_target_va;
    ULONG reloc_target_rva;
    USHORT reloc_type_offset;
    UCHAR reloc_type;
    USHORT reloc_offset;
    ULONG reloc_fixup_rva;
    PUCHAR reloc_fixup;
    IMAGE_BASE_RELOCATION UNALIGNED *reloc_block;
    IMAGE_BASE_RELOCATION UNALIGNED *reloc_block_base;
    ULONG reloc_block_base_va;
    ULONG reloc_count;
    USHORT UNALIGNED *reloc;
    USHORT UNALIGNED *reloc_start;
    ULONG new_rva;
    struct reloc_entry *reloc_entries;
    ULONG reloc_entry_count;
    ULONG reloc_entry_index;
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG section_count;
    ULONG i;
    PUCHAR p;

    mapped_image_end = mapped_image + image_size;
    image_base_va = nt_headers->OptionalHeader.ImageBase;
    reloc_dir_offset = image_directory_offset_and_size(
        nt_headers, IMAGE_DIRECTORY_ENTRY_BASERELOC, &reloc_dir_size, mapped_image, image_size);

    /* Even if relocations do not exist in this PE, then scan the mapped image for
       possible relocations that point to within valid mapped image bounds. */
    if (!reloc_dir_offset || (reloc_dir_offset + reloc_dir_size) > image_size)
    {
        if (nt_headers->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        {
            IMAGE_SECTION_HEADER UNALIGNED *const first_section = IMAGE_FIRST_SECTION(nt_headers);
            ULONG const first_section_va = image_base_va + first_section->VirtualAddress;
            ULONG const image_end_va = image_base_va + nt_headers->OptionalHeader.SizeOfImage;
            for (p = &mapped_image[first_section->PointerToRawData]; p < (mapped_image_end - 4); p++) {
                reloc_target_va = *(ULONG UNALIGNED *)p;

                /* is this a possible rva? */
                if (reloc_target_va >= first_section_va && reloc_target_va < image_end_va) {
                    *(ULONG UNALIGNED *)(hintmap + (p - mapped_image)) |= 0x01010101;

                    reloc_target_rva = reloc_target_va - image_base_va;
                    new_rva = get_new_rva_from_xfrm_table(xfrm_tbl, reloc_target_rva);
                    if (new_rva != reloc_target_rva) {
                        *(ULONG UNALIGNED *)p = image_base_va + new_rva;
                    }

                    /* advance by 3 so next loop we advance to the next dword */
                    p += 4-1;
                }
            }
        }

        return;
    }
    
    /* update the hint map with the base reloc directory */
    memset(&hintmap[reloc_dir_offset], 0x03, reloc_dir_size);

    /* allocate memory for reloc cache entries */
    reloc_entries = VirtualAlloc(NULL, sizeof(struct reloc_entry) * (reloc_dir_size / sizeof(USHORT)),
                                 MEM_COMMIT, PAGE_READWRITE);
    if (!reloc_entries) {
        return;
    }

    /* loop through the relocation table, updating the new rvas. */
    reloc_entry_count = 0;
    reloc_block = (IMAGE_BASE_RELOCATION UNALIGNED *)&mapped_image[reloc_dir_offset];
    reloc_dir_remaining = (LONG)reloc_dir_size;
    while (reloc_dir_remaining > 0
        && reloc_block->SizeOfBlock <= (ULONG)reloc_dir_remaining
        && reloc_block->SizeOfBlock > sizeof(IMAGE_BASE_RELOCATION)) 
    {
        if (((PUCHAR)reloc_block + sizeof(IMAGE_BASE_RELOCATION)) > mapped_image_end) {
            throw_pe_fmt_exception();
        }

        reloc_block_base = (IMAGE_BASE_RELOCATION UNALIGNED *)(mapped_image +
            image_rva_to_file_offset(nt_headers, reloc_block->VirtualAddress, mapped_image, image_size));
        if (reloc_block_base) {

            reloc_block_base_va = reloc_block->VirtualAddress + image_base_va;
            reloc = (PUSHORT)((ULONG_PTR)reloc_block + sizeof(IMAGE_BASE_RELOCATION));
            reloc_count = (reloc_block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
            while (reloc_count--) {
                if ((PUCHAR)reloc > (mapped_image_end - sizeof(USHORT))) {
                    throw_pe_fmt_exception();
                }

                reloc_type_offset = *reloc;
                reloc_type = (UCHAR)(reloc_type_offset >> 12);
                if (reloc_type != IMAGE_REL_BASED_ABSOLUTE) {

                    reloc_offset = (USHORT)(reloc_type_offset & 0xFFF);
                    reloc_fixup = (PUCHAR)reloc_block_base + reloc_offset;
                    reloc_fixup_rva = reloc_block_base_va + reloc_offset - image_base_va;

                    reloc_entries[reloc_entry_count].rva = get_new_rva_from_xfrm_table(xfrm_tbl, reloc_fixup_rva);
                    reloc_entries[reloc_entry_count].type = reloc_type;

                    switch (reloc_type) {

                    case IMAGE_REL_BASED_HIGH:
                    case IMAGE_REL_BASED_LOW:
                        if (reloc_fixup < mapped_image_end) {
                            *(USHORT UNALIGNED *)(hintmap + (reloc_fixup - mapped_image)) |= 0x0101;
                        }
                        break;

                    case IMAGE_REL_BASED_HIGHLOW:
                        if (reloc_fixup < (mapped_image_end - sizeof(LONG))) {
                            *(ULONG UNALIGNED *)(hintmap + (reloc_fixup - mapped_image)) |= 0x01010101;

                            reloc_target_va = *(ULONG UNALIGNED *)reloc_fixup;
                            reloc_target_rva = reloc_target_va - image_base_va;

                            new_rva = get_new_rva_from_xfrm_table(xfrm_tbl, reloc_target_rva);
                            if (new_rva != reloc_target_rva) {
                                *(ULONG UNALIGNED *)reloc_fixup = image_base_va + new_rva;
                            }
                        }
                        break;

                    case IMAGE_REL_BASED_HIGHADJ:
                        if (reloc_fixup < mapped_image_end) {
                            *(USHORT UNALIGNED *)(hintmap + (reloc_fixup - mapped_image)) |= 0x0101;
                        }

                        ++reloc;
                        --reloc_count;

                        reloc_entries[reloc_entry_count].hiadj = *reloc;
                        break;
                    }

                    ++reloc_entry_count;
                }

                ++reloc;
            }
        }

        reloc_dir_remaining -= reloc_block->SizeOfBlock;
        reloc_block = (IMAGE_BASE_RELOCATION UNALIGNED *)((ULONG_PTR)reloc_block + reloc_block->SizeOfBlock);
    }

    /* sort reloc entries by rva. */
    if (reloc_entry_count > 1) {
        qsort(reloc_entries, reloc_entry_count, sizeof(struct reloc_entry), reloc_entry_compare);
    }

    reloc_dir_remaining = (LONG)reloc_dir_size;

    /* calculate the remaining reloc directory size using the ".reloc" section */
    section_table = IMAGE_FIRST_SECTION(nt_headers);
    section_count = nt_headers->FileHeader.NumberOfSections;
    for (i = 0; i < section_count; i++) {
        if ((PUCHAR)&section_table[i] < mapped_image
            || (PUCHAR)&section_table[i] > (mapped_image_end - sizeof(IMAGE_SECTION_HEADER))) {
            throw_pe_fmt_exception();
        }

        /* check for ".reloc  " */
        if (strnicmp((char *)section_table[i].Name, ".reloc  ", IMAGE_SIZEOF_SHORT_NAME) == 0) {
            if (reloc_dir_offset >= section_table[i].PointerToRawData
                && reloc_dir_offset < (section_table[i].PointerToRawData +
                                        section_table[i].SizeOfRawData)) {
                reloc_dir_remaining =
                    (section_table[i].PointerToRawData + section_table[i].SizeOfRawData)
                        - reloc_dir_offset;
            }
        }
    }

    reloc_dir_remaining &= ~1; /* make even value */
    reloc_block = (IMAGE_BASE_RELOCATION UNALIGNED *)(mapped_image + reloc_dir_offset);
    reloc_entry_index = 0;
    while (reloc_dir_remaining > sizeof(IMAGE_BASE_RELOCATION)
        && reloc_entry_index < reloc_entry_count)
    {
        reloc_block->VirtualAddress = (DWORD)(reloc_entries[reloc_entry_index].rva & 0xFFFFF000);
        reloc_start = (PUSHORT)((ULONG_PTR)reloc_block + sizeof(IMAGE_BASE_RELOCATION));
        reloc = reloc_start;
        reloc_dir_remaining -= sizeof(IMAGE_BASE_RELOCATION);
        while (reloc_dir_remaining > 0
            && (reloc_entry_index < reloc_entry_count)
            && (reloc_entries[reloc_entry_index].rva & 0xFFFFF000) == reloc_block->VirtualAddress)
        {
            reloc_type_offset = ((USHORT)reloc_entries[reloc_entry_index].type << 12) |
                                ((USHORT)reloc_entries[reloc_entry_index].rva & 0xFFF);
            *reloc++ = reloc_type_offset;

            reloc_dir_remaining -= sizeof(USHORT);

            if (reloc_dir_remaining > 0
                && ((ULONG)reloc_entries[reloc_entry_index].type << 12) == IMAGE_REL_BASED_HIGHADJ) {

                *reloc++ = reloc_entries[reloc_entry_index].hiadj;
                reloc_dir_remaining -= sizeof(USHORT);
            }

            ++reloc_entry_index;
        }

        if (reloc_dir_remaining > 0 && ((ULONG_PTR)reloc & 2) != 0) {
            *reloc++ = 0;
            reloc_dir_remaining -= sizeof(USHORT);
        }

        reloc_block->SizeOfBlock = (ULONG)(((ULONG_PTR)reloc - (ULONG_PTR)reloc_start) + sizeof(IMAGE_BASE_RELOCATION));
        reloc_block = (IMAGE_BASE_RELOCATION UNALIGNED *)((ULONG_PTR)reloc_block + reloc_block->SizeOfBlock);
    }

    VirtualFree(reloc_entries, 0, MEM_RELEASE);
}

static void patch_transform_PE_infer_relocs_x86(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, BYTE *hintmap)
{
    const ULONG image_base_va = nt_headers->OptionalHeader.ImageBase;
    const ULONG image_end_va = image_base_va + nt_headers->OptionalHeader.SizeOfImage;
    const PUCHAR mapped_image_end = &mapped_image_base[mapped_image_size];
    PUCHAR p = &mapped_image_base[nt_headers->OptionalHeader.SizeOfHeaders];
    for (; p < (mapped_image_end - 4); p++) {

        /* Is this a possible valid rva? */
        const ULONG reloc_target_va = *(ULONG UNALIGNED *)p;
        if (reloc_target_va >= image_base_va && reloc_target_va < image_end_va) {

            /* Check the hintmap. */
            const ULONG infer_mask =  *(ULONG UNALIGNED *)(hintmap + (p - mapped_image_base));
            if ((infer_mask & 0x02020202) == 0) {
                
                const ULONG reloc_target_rva = reloc_target_va - image_base_va;
                const ULONG new_rva = get_new_rva_from_xfrm_table(xfrm_tbl, reloc_target_rva);
                if (new_rva != reloc_target_rva) {
                    *(ULONG UNALIGNED *)p = image_base_va + new_rva;
                }

                *(ULONG UNALIGNED *)(hintmap + (p - mapped_image_base)) = infer_mask | 0x01010101;

                /* advance by 3 so next loop we advance to the next dword */
                p += 4-1;
            }
        }
    }
}

static void patch_transform_PE_infer_relocs(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, BYTE *hintmap)
{
    ULONG reloc_dir_offset;
    ULONG reloc_dir_size = 0;

    reloc_dir_offset = image_directory_offset_and_size(nt_headers, IMAGE_DIRECTORY_ENTRY_BASERELOC,
                                                &reloc_dir_size, mapped_image_base, mapped_image_size);
    if (reloc_dir_offset && reloc_dir_size + reloc_dir_offset <= mapped_image_size) {
        patch_transform_PE_relocations(nt_headers, mapped_image_base, mapped_image_size, xfrm_tbl, hintmap);
    }

    switch(nt_headers->FileHeader.Machine) {
    case IMAGE_FILE_MACHINE_I386:
        patch_transform_PE_infer_relocs_x86(nt_headers, mapped_image_base, mapped_image_size, xfrm_tbl, hintmap);
        break;

    default:
        break;
    }
}

static void patch_transform_PE_imports(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, BYTE* hintmap)
{
    ULONG import_dir_offset;
    ULONG import_dir_size;
    IMAGE_IMPORT_DESCRIPTOR UNALIGNED *import_desc;
    ULONG thunk_offset;
    IMAGE_THUNK_DATA32 UNALIGNED *thunk_data_start;
    IMAGE_THUNK_DATA32 UNALIGNED *thunk_data;
    IMAGE_IMPORT_BY_NAME UNALIGNED *import_by_name;
    ULONG import_by_name_offset;

    import_dir_offset = image_directory_offset_and_size(nt_headers, IMAGE_DIRECTORY_ENTRY_IMPORT,
                                                    &import_dir_size, mapped_image_base, mapped_image_size);

    /* Does the mapped image contain imports? */
    if (!import_dir_offset || (import_dir_offset + import_dir_size) > mapped_image_size) {
        return;
    }

    /* Update the hint map of the import directory. */
    memset(&hintmap[import_dir_offset], 0x03, import_dir_size);

    /* Loop through the import table, updating the new rvas. */
    import_desc = (IMAGE_IMPORT_DESCRIPTOR UNALIGNED *)(mapped_image_base + import_dir_offset);
    while (import_desc->OriginalFirstThunk) {
        if (import_desc->TimeDateStamp == 0) { /* 0 if not bound */

            thunk_offset = image_rva_to_file_offset(nt_headers, import_desc->OriginalFirstThunk,
                                                    mapped_image_base, mapped_image_size);
            thunk_data_start = (IMAGE_THUNK_DATA32 UNALIGNED *)(mapped_image_base + thunk_offset);
            thunk_data = thunk_data_start;
            while (thunk_data->u1.Ordinal != 0) {
                if (!IMAGE_SNAP_BY_ORDINAL32(thunk_data->u1.Ordinal)) {
                    import_by_name_offset = image_rva_to_file_offset(nt_headers, thunk_data->u1.AddressOfData,
                                                                    mapped_image_base, mapped_image_size);
                    import_by_name = (IMAGE_IMPORT_BY_NAME UNALIGNED *)(mapped_image_base + import_by_name_offset);
                    memset(&hintmap[import_by_name_offset], 0x03,
                        sizeof(IMAGE_IMPORT_BY_NAME) + strlen((char*)import_by_name->Name));
                    thunk_data->u1.AddressOfData = get_new_rva_from_xfrm_table(xfrm_tbl, thunk_data->u1.AddressOfData);
                }
                thunk_data++;
            }
            memset(&hintmap[thunk_offset], 0x03, ((size_t)thunk_data - (size_t)thunk_data_start));

            thunk_offset = image_rva_to_file_offset(nt_headers, import_desc->FirstThunk,
                                                    mapped_image_base, mapped_image_size);
            thunk_data_start = (IMAGE_THUNK_DATA32 UNALIGNED *)(mapped_image_base + thunk_offset);
            thunk_data = thunk_data_start;
            while (thunk_data->u1.Ordinal != 0) {
                if (!IMAGE_SNAP_BY_ORDINAL32(thunk_data->u1.Ordinal)) {
                    import_by_name_offset = image_rva_to_file_offset(nt_headers, thunk_data->u1.AddressOfData,
                                                                    mapped_image_base, mapped_image_size);
                    import_by_name = (IMAGE_IMPORT_BY_NAME UNALIGNED *)(mapped_image_base + import_by_name_offset);
                    memset(&hintmap[import_by_name_offset], 0x03,
                        sizeof(IMAGE_IMPORT_BY_NAME) + strlen((char*)import_by_name->Name));
                    thunk_data->u1.AddressOfData = get_new_rva_from_xfrm_table(xfrm_tbl, thunk_data->u1.AddressOfData);
                }
                thunk_data++;
            }
            memset(&hintmap[thunk_offset], 0x03, ((size_t)thunk_data - (size_t)thunk_data_start));
        }

        import_desc->Name = get_new_rva_from_xfrm_table(xfrm_tbl, import_desc->Name);
        import_desc->OriginalFirstThunk = get_new_rva_from_xfrm_table(xfrm_tbl, import_desc->OriginalFirstThunk);
        import_desc->FirstThunk = get_new_rva_from_xfrm_table(xfrm_tbl, import_desc->FirstThunk);

        import_desc++;
    }
}

static void patch_transform_PE_exports(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, BYTE *hintmap)
{
    IMAGE_EXPORT_DIRECTORY UNALIGNED *export_directory;
    ULONG UNALIGNED *export;
    ULONG export_count;
    PUCHAR mapped_image_end;
    ULONG offset;
    ULONG export_dir_offset;
    ULONG export_dir_size;

    mapped_image_end = mapped_image_base + mapped_image_size;

    export_dir_offset = image_directory_offset_and_size(nt_headers, IMAGE_DIRECTORY_ENTRY_EXPORT,
                            &export_dir_size, mapped_image_base, mapped_image_size);

    /* Does the mapped image contain exports? */
    if (!export_dir_offset || (export_dir_offset + export_dir_size) > mapped_image_size) {
        return;
    }

    /* Update the hint map of the export directory. */
    memset(hintmap + export_dir_offset, 0x03, export_dir_size);

    export_directory = (IMAGE_EXPORT_DIRECTORY UNALIGNED *)(mapped_image_base + export_dir_offset);

    /* Update the hint map and rvas for the AddressOfFunctions table. */
    export_count = export_directory->NumberOfFunctions;
    offset = image_rva_to_file_offset(nt_headers, export_directory->AddressOfFunctions,
                                      mapped_image_base, mapped_image_size);
    if ((offset + export_count * sizeof(ULONG)) <= mapped_image_size) {
        memset(&hintmap[offset], 0x03, export_count * sizeof(ULONG));
    }
    export = (PULONG)(mapped_image_base + offset);
    while (export_count--) {
        if ((PUCHAR)&export[1] > mapped_image_end) {
            break;
        }
        *export = get_new_rva_from_xfrm_table(xfrm_tbl, *export);
        export++;
    }

    /* Update the hint map and rvas for the AddressOfNames table. */
    export_count = export_directory->NumberOfNames;
    offset = image_rva_to_file_offset(nt_headers, export_directory->AddressOfNames,
                                      mapped_image_base, mapped_image_size);
    if ((offset + export_count * sizeof(ULONG)) <= mapped_image_size) {
        memset(&hintmap[offset], 0x03, export_count * sizeof(ULONG));
    }
    export = (PULONG)(mapped_image_base + offset);
    while (export_count--) {
        if ((PUCHAR)&export[1] > mapped_image_end) {
            break;
        }
        *export = get_new_rva_from_xfrm_table(xfrm_tbl, *export);
        export++;
    }

    /* Update the hint map for the AddressOfNameOrdinals table. */
    export_count = export_directory->NumberOfNames;
    offset = image_rva_to_file_offset(nt_headers, export_directory->AddressOfNameOrdinals,
                                      mapped_image_base, mapped_image_size);
    if ((offset + export_count * sizeof(USHORT)) <= mapped_image_size) {
        memset(&hintmap[offset], 0x03, export_count * sizeof(USHORT));
    }

    /* Update export directory rvas. */
    export_directory->Name = get_new_rva_from_xfrm_table(xfrm_tbl, export_directory->Name);
    export_directory->AddressOfFunctions = get_new_rva_from_xfrm_table(xfrm_tbl, export_directory->AddressOfFunctions);
    export_directory->AddressOfNames = get_new_rva_from_xfrm_table(xfrm_tbl, export_directory->AddressOfNames);
    export_directory->AddressOfNameOrdinals = get_new_rva_from_xfrm_table(xfrm_tbl, export_directory->AddressOfNameOrdinals);
}

static void patch_transform_PE_relative_jmps(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, BYTE *hintmap)
{
    ULONG image_size;
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG section_count;
    PBYTE section_start, section_end;
    ULONG section_size;
    ULONG section_offset;
    ULONG section_rva;
    LONG disp, new_disp;
    ULONG ins_rva, target_rva;
    ULONG new_ins_rva, new_target_rva;
    ULONG target_offset;
    ULONG i, j;
    PBYTE p, hint;
    BYTE opcode;
    BOOL needs_patch;

    image_size = nt_headers->OptionalHeader.SizeOfImage;
    section_table = IMAGE_FIRST_SECTION(nt_headers);
    section_count = nt_headers->FileHeader.NumberOfSections;

    for (i = 0; i < section_count; i++) {
        if ((PUCHAR)&section_table[i] < mapped_image_base
            || (PUCHAR)&section_table[i+1] > &mapped_image_base[mapped_image_size]) {
            throw_pe_fmt_exception();
        }

        /* If this section is executable, then scan it for relative jump instructions. */
        if (section_table[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {

            section_rva = section_table[i].VirtualAddress;
            section_size = min(section_table[i].Misc.VirtualSize, section_table[i].SizeOfRawData);
            section_offset = section_table[i].PointerToRawData;
            section_start = (PUCHAR)mapped_image_base + section_offset;

            if (section_offset < mapped_image_size
                && (section_offset + section_size) <= mapped_image_size
                && nt_headers->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
            {
                section_end = section_start + section_size - 5; /* 5 is size of relative jmp instruction */

                for (p = section_start; p < section_end; p++)
                {
                    if ((*p == 0xE9) /* jmp rel32 (E9) */
                        || (*p == 0x0F && (p[1] & 0xF0) == 0x80 && (++p < section_end)) /* jcc rel32 (0F 8?) */
                        )
                    {
                        /* Check the hint map in case this has already been patched. */
                        needs_patch = FALSE;
                        hint = &hintmap[section_offset + ((ULONG_PTR)p - (ULONG_PTR)section_start) - 1];
                        if ((*p & 0xF0) != 0x80 || (*hint & 1) == 0) {
                            hint++;
                            j = 0;
                            while ((*hint++ & 1) == 0) {
                                if (++j >= 5) {
                                    needs_patch = TRUE;
                                    break;
                                }
                            }
                        }

                        if (needs_patch) {
                            disp = *(LONG UNALIGNED *)(p + 1);
                            if (disp > 127 || disp < -128)
                            {
                                ins_rva = section_rva + (ULONG)((p + 5) - section_start);
                                target_rva = ins_rva + disp;
                                if (target_rva < image_size)
                                {
                                    target_offset = image_rva_to_file_offset(nt_headers, target_rva,
                                                                mapped_image_base, mapped_image_size);
                                    if ((hintmap[target_offset] & 1) == 0) {
                                        new_target_rva = get_new_rva_from_xfrm_table(xfrm_tbl, target_rva);
                                        new_ins_rva = get_new_rva_from_xfrm_table(xfrm_tbl, ins_rva);
                                        new_disp = new_target_rva - new_ins_rva;
                                        if (new_disp > 127 || new_disp < -128) {
                                            if (new_disp != disp) {
                                                *(LONG UNALIGNED *)(p + 1) = new_disp;
                                            }
                                        }
                                        else {
                                            opcode = *p;
                                            if (opcode == 0xE9) {
                                                *p = 0xEB;
                                                p[1] = (char)new_disp;
                                            } else {
                                                *p = (char)new_disp;
                                                *(p - 1) = (opcode & 0x0F) | 0x70;
                                            }
                                        }
                                        p += 4;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

static void patch_transform_PE_relative_calls(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image_base, ULONG mapped_image_size,
    struct patch_transform_table *xfrm_tbl, PUCHAR hintmap)
{
    IMAGE_SECTION_HEADER UNALIGNED *section_table;
    ULONG sizeof_image;
    PUCHAR section_start, section_end;
    ULONG section_size;
    ULONG section_offset;
    ULONG section_rva;
    LONG disp, new_disp;
    ULONG ins_rva, target_rva;
    ULONG new_ins_rva, new_target_rva;
    ULONG target_offset;
    ULONG i, j;
    PUCHAR p, hint;
    BOOL needs_patch;

    sizeof_image = nt_headers->OptionalHeader.SizeOfImage;
    section_table = IMAGE_FIRST_SECTION(nt_headers);

    for (i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
        if ((PUCHAR)&section_table[i] < mapped_image_base
            || (PUCHAR)&section_table[i+1] > &mapped_image_base[mapped_image_size]) {
            throw_pe_fmt_exception();
        }

        /* If this section is executable, then scan it for relative jump instructions. */
        if (section_table[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {

            section_rva = section_table[i].VirtualAddress;
            section_size = min(section_table[i].Misc.VirtualSize, section_table[i].SizeOfRawData);
            section_offset = section_table[i].PointerToRawData;
            section_start = (PUCHAR)mapped_image_base + section_offset;

            if (section_offset < mapped_image_size
                && (section_offset + section_size) <= mapped_image_size
                && nt_headers->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
            {
                section_end = section_start + section_size - 5; /* 5 is size of relative jmp instruction */

                for (p = section_start; p < section_end; p++)
                {
                    if (*p == 0xE8) /* call rel32 (E8) */
                    {
                        /* Check the hint map in case this has already been patched. */
                        needs_patch = FALSE;
                        j = 0;
                        hint = &hintmap[section_offset + (p - section_start)];
                        while ((*hint++ & 1) == 0) {
                            if (++j >= 5) {
                                needs_patch = TRUE;
                                break;
                            }
                        }

                        if (needs_patch) {
                            disp = *(LONG UNALIGNED *)(p + 1);
                            ins_rva = section_rva + (ULONG)((ULONG_PTR)p - (ULONG_PTR)section_start) + 5;
                            target_rva = ins_rva + disp;
                            if (target_rva < sizeof_image) {
                                target_offset = image_rva_to_file_offset(nt_headers, target_rva,
                                                            mapped_image_base, mapped_image_size);
                                if ((hintmap[target_offset] & 1) == 0) {
                                    new_target_rva = get_new_rva_from_xfrm_table(xfrm_tbl, target_rva);
                                    new_ins_rva = get_new_rva_from_xfrm_table(xfrm_tbl, ins_rva);
                                    new_disp = new_target_rva - new_ins_rva;
                                    if (new_disp != disp) {
                                        *(LONG UNALIGNED *)(p + 1) = new_disp;
                                    }
                                    p += 4;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

struct transform_resource_context {         /* i386 | x64  */
    struct patch_transform_table* xfrm_tbl; /* 0x00 | 0x00 */
    PUCHAR res_dir_base;                    /* 0x04 | 0x08 */
    PUCHAR res_dir_end;                     /* 0x08 | 0x10 */
    ULONG res_dir_size;                     /* 0x0C | 0x18 */
    ULONG cursor;                           /* 0x10 | 0x1C */
    ULONG new_res_time;                     /* 0x14 | 0x20 */
    ULONG sizeof_image;                     /* 0x18 | 0x24 */
    PUCHAR image_base;                      /* 0x1C | 0x28 */
    ULONG old_rva;                          /* 0x20 | 0x30 */
    ULONG new_rva;                          /* 0x24 | 0x34 */
    BOOL out_of_bounds;                     /* 0x28 | 0x38 */
};

static void transform_resources_recursive_old_compat(
    struct transform_resource_context *ctx,
    IMAGE_RESOURCE_DIRECTORY UNALIGNED *res_dir)
{
    ULONG entry_count, i;
    IMAGE_RESOURCE_DIRECTORY_ENTRY UNALIGNED *entry;
    IMAGE_RESOURCE_DATA_ENTRY UNALIGNED *res_data;
    IMAGE_RESOURCE_DIRECTORY UNALIGNED *recursive_res_dir;
    ULONG offset_to_data;
    ULONG name_offset;
    ULONG new_offset;
    ULONG new_rva;

    if (((PUCHAR)res_dir + sizeof(IMAGE_RESOURCE_DIRECTORY)) < ctx->res_dir_end) {

        ctx->cursor += sizeof(IMAGE_RESOURCE_DIRECTORY);

        if (ctx->cursor <= ctx->res_dir_size) {

            if (res_dir->TimeDateStamp != ctx->new_res_time) {
                res_dir->TimeDateStamp = ctx->new_res_time;
            }

            entry_count = res_dir->NumberOfNamedEntries + res_dir->NumberOfIdEntries;
            entry = (IMAGE_RESOURCE_DIRECTORY_ENTRY UNALIGNED *)((PUCHAR)res_dir + sizeof(IMAGE_RESOURCE_DIRECTORY));

            for (i = 0; i < entry_count; i++, entry++) {
                if ((PUCHAR)entry >= (ctx->res_dir_end - sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))) {
                    break;
                }

                offset_to_data = entry->OffsetToData & ~IMAGE_RESOURCE_DATA_IS_DIRECTORY;
                if (entry->OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY) {
                    /* Recurse into next resource directory. */
                    recursive_res_dir = (IMAGE_RESOURCE_DIRECTORY UNALIGNED *)&ctx->res_dir_base[offset_to_data];
                    if ((PUCHAR)recursive_res_dir >= ctx->image_base) {
                        transform_resources_recursive_old_compat(ctx, recursive_res_dir);
                    }

                } else {
                    /* Update rva of valid resource data entry. */
                    res_data = (IMAGE_RESOURCE_DATA_ENTRY UNALIGNED *)&ctx->res_dir_base[offset_to_data];
                    if ((PUCHAR)res_data > ctx->res_dir_base
                        && (PUCHAR)res_data < (ctx->res_dir_end - sizeof(IMAGE_RESOURCE_DATA_ENTRY)))
                    {
                        ctx->cursor += res_data->Size;
                        if (ctx->cursor > ctx->res_dir_size) {
                            return;
                        }

                        new_rva = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, res_data->OffsetToData);
                        if (res_data->OffsetToData != new_rva) {
                            res_data->OffsetToData = new_rva;
                        }
                    }
                }

                new_offset = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, offset_to_data + ctx->old_rva) - ctx->new_rva;
                if (offset_to_data != new_offset) {
                    entry->OffsetToData ^= (new_offset ^ entry->OffsetToData) & ~IMAGE_RESOURCE_DATA_IS_DIRECTORY;
                }

                if (entry->Name & IMAGE_RESOURCE_NAME_IS_STRING) {
                    name_offset = entry->Name & ~IMAGE_RESOURCE_NAME_IS_STRING;
                    new_offset = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, name_offset + ctx->old_rva) - ctx->new_rva;
                    if (name_offset != new_offset) {
                        entry->Name ^= (new_offset ^ entry->Name) & ~IMAGE_RESOURCE_NAME_IS_STRING;
                    }
                }

                ctx->cursor += sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
                if (ctx->cursor > ctx->res_dir_size) {
                    return;
                }
            }
        }
    }
}

static void patch_transform_PE_resources_old_compat(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, ULONG image_size,
    ULONG new_res_time, struct patch_transform_table *xfrm_tbl)
{
    struct transform_resource_context ctx;
    const PUCHAR mapped_image_end = &mapped_image[image_size];

    ctx.res_dir_base = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                                      &ctx.res_dir_size, mapped_image, image_size);
    if (ctx.res_dir_base && ctx.res_dir_size >= sizeof(IMAGE_RESOURCE_DIRECTORY))
    {
        ctx.res_dir_end = ctx.res_dir_base + ctx.res_dir_size;
        if (ctx.res_dir_end >= mapped_image_end) {
            ctx.res_dir_end = mapped_image_end;
        }

        ctx.cursor = 0;
        ctx.out_of_bounds = FALSE;

        ctx.old_rva = image_directory_rva_and_size(nt_headers, IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                                   NULL, mapped_image, image_size);
        ctx.new_rva = get_new_rva_from_xfrm_table(xfrm_tbl, ctx.old_rva);

        ctx.new_res_time = new_res_time;
        ctx.sizeof_image = nt_headers->OptionalHeader.SizeOfImage;
        ctx.image_base = mapped_image;
        ctx.xfrm_tbl = xfrm_tbl;

        if (ctx.res_dir_base >= mapped_image) {
            transform_resources_recursive_old_compat(&ctx,
                (IMAGE_RESOURCE_DIRECTORY UNALIGNED *)ctx.res_dir_base);
        }
    }
}

static void transform_resources_recursive(
    struct transform_resource_context *ctx,
    IMAGE_RESOURCE_DIRECTORY UNALIGNED *res_dir)
{
    IMAGE_RESOURCE_DIRECTORY_ENTRY UNALIGNED *entry;
    IMAGE_RESOURCE_DATA_ENTRY UNALIGNED *res_data;
    IMAGE_RESOURCE_DIRECTORY UNALIGNED *recursive_res_dir;
    ULONG i;
    ULONG entry_count;
    ULONG offset_to_data;
    ULONG name_offset;
    ULONG new_offset;
    ULONG new_rva;

    if ((PUCHAR)res_dir < ctx->res_dir_base
        || ((PUCHAR)res_dir + sizeof(IMAGE_RESOURCE_DIRECTORY)) > ctx->res_dir_end) {
        ctx->out_of_bounds = TRUE;
        return;
    }

    ctx->cursor += sizeof(IMAGE_RESOURCE_DIRECTORY);
    if (ctx->cursor > ctx->res_dir_size) {
        ctx->out_of_bounds = TRUE;
        return;
    }

    if (res_dir->TimeDateStamp != ctx->new_res_time) {
        res_dir->TimeDateStamp = ctx->new_res_time;
    }

    entry = (IMAGE_RESOURCE_DIRECTORY_ENTRY UNALIGNED *)((PUCHAR)res_dir + sizeof(IMAGE_RESOURCE_DIRECTORY));
    entry_count = res_dir->NumberOfNamedEntries + res_dir->NumberOfIdEntries;

    for (i = 0; i < entry_count; i++, entry++) {
        if (((PUCHAR)entry + sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)) > ctx->res_dir_end) {
            ctx->out_of_bounds = TRUE;
            return;
        }

        ctx->cursor += sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
        if (ctx->cursor > ctx->res_dir_size) {
            ctx->out_of_bounds = TRUE;
            return;
        }

        offset_to_data = entry->OffsetToData & ~IMAGE_RESOURCE_DATA_IS_DIRECTORY;
        if (entry->OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY) {
            if (ctx->res_dir_size >= sizeof(IMAGE_RESOURCE_DIRECTORY)
                && (offset_to_data + sizeof(IMAGE_RESOURCE_DIRECTORY)) <= ctx->res_dir_size) {

                /* Recurse into next resource directory. */
                recursive_res_dir = (IMAGE_RESOURCE_DIRECTORY UNALIGNED *)&ctx->res_dir_base[offset_to_data];
                if ((PUCHAR)recursive_res_dir >= ctx->res_dir_base) {
                    transform_resources_recursive(ctx, recursive_res_dir);
                } else {
                    ctx->out_of_bounds = TRUE;
                }

                if (ctx->cursor > ctx->res_dir_size) {
                    ctx->out_of_bounds = TRUE;
                    return;
                }
            } else {
                ctx->out_of_bounds = TRUE;
            }

        } else {
            /* Update rva of valid resource data entry. */
            res_data = (IMAGE_RESOURCE_DATA_ENTRY UNALIGNED *)&ctx->res_dir_base[offset_to_data];
            if ((PUCHAR)res_data < ctx->res_dir_base
                && ((PUCHAR)res_data + sizeof(IMAGE_RESOURCE_DATA_ENTRY)) > ctx->res_dir_end) {
                ctx->out_of_bounds = TRUE;
            } else {

                new_rva = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, res_data->OffsetToData);
                if (res_data->OffsetToData != new_rva) {
                    res_data->OffsetToData = new_rva;
                }
            }
        }

        new_offset = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, offset_to_data + ctx->old_rva) - ctx->new_rva;
        if (offset_to_data != new_offset) {
            entry->OffsetToData ^= (new_offset ^ entry->OffsetToData) & ~IMAGE_RESOURCE_DATA_IS_DIRECTORY;
        }

        if (entry->Name & IMAGE_RESOURCE_NAME_IS_STRING) {
            name_offset = entry->Name & ~IMAGE_RESOURCE_NAME_IS_STRING;
            new_offset = get_new_rva_from_xfrm_table(ctx->xfrm_tbl, name_offset + ctx->old_rva) - ctx->new_rva;
            if (entry->NameOffset != new_offset) {
                entry->Name ^= (new_offset ^ entry->Name) & ~IMAGE_RESOURCE_NAME_IS_STRING;
            }
        }
    }
}

static BOOL patch_transform_PE_resources(
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers, PUCHAR mapped_image, SIZE_T image_size,
    ULONG new_res_time, struct patch_transform_table *xfrm_tbl)
{
    BOOL result;
    struct transform_resource_context ctx;

    ctx.res_dir_base = image_directory_mapped_address(nt_headers, IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                                      &ctx.res_dir_size, mapped_image, image_size);
    if (ctx.res_dir_base) {
        ctx.res_dir_end = ctx.res_dir_base + ctx.res_dir_size;
        ctx.cursor = 0;
        ctx.image_base = mapped_image;
        ctx.sizeof_image = nt_headers->OptionalHeader.SizeOfImage;
        ctx.old_rva = image_directory_rva_and_size(nt_headers, IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                                   NULL, mapped_image, image_size);
        ctx.new_rva = get_new_rva_from_xfrm_table(xfrm_tbl, ctx.old_rva);
        ctx.new_res_time = new_res_time;
        ctx.xfrm_tbl = xfrm_tbl;
        ctx.out_of_bounds = FALSE;
        transform_resources_recursive(&ctx, (IMAGE_RESOURCE_DIRECTORY UNALIGNED *)ctx.res_dir_base);
        result = !ctx.out_of_bounds;
    } else {
        result = TRUE;
    }

    return result;
}

static BOOL patch_coff_image(
    PULONG transform_option_flags, IMAGE_NT_HEADERS32 UNALIGNED *nt_headers,
    PUCHAR old_file_buffer, ULONG old_file_size, ULONG new_res_time,
    struct patch_transform_table* xfrm_tbl, unsigned char *hintmap)
{
    PUCHAR local_hintmap = NULL;
    const ULONG transform_flags = *transform_option_flags;

    /* initialize the hintmap */
    if (!hintmap) {
        local_hintmap = VirtualAlloc(NULL, old_file_size, MEM_COMMIT, PAGE_READWRITE);
        if (!local_hintmap) {
            return FALSE;
        }
        hintmap = local_hintmap;
    }
    memset(hintmap, 0x00, old_file_size);

    /* Order of transformations may matter here! */
    if (transform_flags & (PATCH_TRANSFORM_PE_RESOURCE_2 | PATCH_TRANSFORM_PE_IRELOC_2))
    {
        patch_transform_PE_mark_non_executable(nt_headers, old_file_buffer, old_file_size, hintmap, FALSE);

        if ((transform_flags & PATCH_TRANSFORM_NO_IMPORTS) == 0) {
            patch_transform_PE_imports(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_EXPORTS) == 0) {
            patch_transform_PE_exports(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RESOURCE) == 0) {
            patch_transform_PE_resources(nt_headers, old_file_buffer, old_file_size, new_res_time, xfrm_tbl);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RELOCS) == 0) {
            if (transform_flags & PATCH_TRANSFORM_PE_IRELOC_2) {
                patch_transform_PE_infer_relocs(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
            } else {
                patch_transform_PE_relocations(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
            }
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RELJMPS) == 0) {
            patch_transform_PE_relative_jmps(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RELCALLS) == 0) {
            patch_transform_PE_relative_calls(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

    } else {

        patch_transform_PE_mark_non_executable(nt_headers, old_file_buffer, old_file_size, hintmap, TRUE);

        if ((transform_flags & PATCH_TRANSFORM_NO_RELOCS) == 0) {
            patch_transform_PE_relocations(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_IMPORTS) == 0) {
            patch_transform_PE_imports(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_EXPORTS) == 0) {
            patch_transform_PE_exports(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RELJMPS) == 0) {
            patch_transform_PE_relative_jmps(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RELCALLS) == 0) {
            patch_transform_PE_relative_calls(nt_headers, old_file_buffer, old_file_size, xfrm_tbl, hintmap);
        }

        if ((transform_flags & PATCH_TRANSFORM_NO_RESOURCE) == 0) {
            patch_transform_PE_resources_old_compat(nt_headers, old_file_buffer, old_file_size, new_res_time, xfrm_tbl);
        }
    }

    if (local_hintmap != NULL) {
        VirtualFree(local_hintmap, 0, MEM_RELEASE);
    }

    return TRUE;
}

/* Performs patches on 32-bit PE images. */
static DWORD perform_patches_on_old_file_image(
    PULONG transform_option_flags, PVOID old_file_buffer, ULONG old_file_size,
    ULONG new_file_res_time, struct patch_transform_table* xfrm_tbl)
{
    DWORD err = ERROR_SUCCESS;
    IMAGE_NT_HEADERS32 UNALIGNED *nt_headers;

    nt_headers = image_get_nt_headers(old_file_buffer, old_file_size);
    if (nt_headers) {
        *transform_option_flags |= PATCH_EXTRA_HAS_PATCH_TRANSFORMS;
        if (!patch_coff_image(transform_option_flags, nt_headers, old_file_buffer,
                                old_file_size, new_file_res_time, xfrm_tbl, NULL)) {
            err = GetLastError();
        }
    } else {
        err = ERROR_BAD_EXE_FORMAT;
    }

    return err;
}


BOOL progress_callback_wrapper(
    PPATCH_PROGRESS_CALLBACK progress_fn, void *progress_ctx, ULONG current, ULONG maximum)
{
    if (progress_fn != NULL && !progress_fn(progress_ctx, current, maximum)) {
        if (GetLastError() == ERROR_SUCCESS) {
            SetLastError(ERROR_CANCELLED);
        }
        return FALSE;
    }
    return TRUE;
}

DWORD apply_patch_to_file_by_buffers(
    const BYTE *patch_file_view, const ULONG patch_file_size,
    const BYTE *old_file_view, ULONG old_file_size,
    BYTE **pnew_file_buf, const ULONG new_file_buf_size,
    ULONG *new_file_actual_size, FILETIME *new_file_time,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx)
{
    DWORD err = ERROR_SUCCESS;
    struct patch_file_header ph = {0};
    struct old_file_info *file_info;
    struct old_file_info *found_file;
    ULONG fileno;
    ULONG i;
    int normalize_result;
    BOOL patched;
    ULONG old_crc32;
    ULONG patched_crc32;
    ULONG buf_size;
    BYTE *in_new_file_buf = NULL;
    BYTE *out_new_file_buf = NULL;
    BYTE *new_file_buf = NULL;
    BYTE *old_file_buf = NULL;
    BYTE *decode_buf = NULL;
    BYTE *retain_buffer = NULL;

    if (new_file_actual_size != NULL) {
        *new_file_actual_size = 0;
    }

    if (new_file_time != NULL) {
        new_file_time->dwLowDateTime = 0;
        new_file_time->dwHighDateTime = 0;
    }

    if (pnew_file_buf == NULL) {
        if (!(apply_option_flags & APPLY_OPTION_TEST_ONLY)) {
            return ERROR_INVALID_PARAMETER;
        }

    } else {
        in_new_file_buf = *pnew_file_buf;
    }

    if (old_file_view == NULL) {
        old_file_size = 0;
    }

    /* read the patch file header */
    if (read_patch_header(&ph, patch_file_view, patch_file_size)) {
        err = ph.err;
        goto cleanup;
    }

    if ((ph.flags & PATCH_OPTION_NO_TIMESTAMP) == 0) {
        TRACE("new file time is %lX\n", ph.new_file_time);
        if (new_file_time != NULL) {
            if (ph.new_file_time) {
                posix_time_to_file_time(ph.new_file_time, new_file_time);
            }
        }
    }

    if (new_file_actual_size != NULL) {
        *new_file_actual_size = (ULONG)ph.patched_size;
    }

    if (in_new_file_buf != NULL && new_file_buf_size < ph.patched_size) {
        err = ERROR_INSUFFICIENT_BUFFER;
        goto cleanup;
    }

    buf_size = ph.patched_size + old_file_size;
    decode_buf = in_new_file_buf;
    if (in_new_file_buf == NULL || new_file_buf_size < buf_size)
    {
        /* decode_buf must have room for both files, so allocate a new buffer if
         * necessary. This will be returned to the caller if new_file_buf == NULL */
        decode_buf = VirtualAlloc(NULL, buf_size, MEM_COMMIT, PAGE_READWRITE);
        if (decode_buf == NULL) {
            err = GetLastError();
            goto cleanup;
        }
    }

    new_file_buf = &decode_buf[old_file_size];
    if (old_file_view != NULL) {
        old_file_buf = decode_buf;
        memcpy(old_file_buf, old_file_view, old_file_size);
    }

    /* Make initial progress callback. */
    if (!progress_callback_wrapper(progress_fn, progress_ctx, 0, ph.patched_size)) {
        err = GetLastError();
        goto cleanup;
    }

    patched = FALSE;
    retain_buffer = NULL;
    found_file = NULL;

    /* Check if the old file matches the new file. */
    if (old_file_size == ph.patched_size)
    {
        file_info = &ph.file_table[0];
        if (file_info->retain_range_count)
        {
            /* Save the new file retain ranges for applying later. */
            retain_buffer = save_retained_ranges(old_file_buf, old_file_size,
                file_info->retain_range_array, file_info->retain_range_count, TRUE);
            if (!retain_buffer) {
                err = GetLastError();
                goto cleanup;
            }

            /* Zero out retain ranges for crc32 checksum verification. */
            for (i = 0; i < file_info->retain_range_count; i++) {
                if (file_info->retain_range_array[i].OffsetInNewFile <= old_file_size
                    && (file_info->retain_range_array[i].OffsetInNewFile
                        + file_info->retain_range_array[i].LengthInBytes) <= old_file_size) {
                    memset(&old_file_buf[file_info->retain_range_array[i].OffsetInNewFile],
                           0,
                           file_info->retain_range_array[i].LengthInBytes);
                    patched = TRUE;
                }
            }
        }

        /* Verify if the file matches the expected crc32 checksum. */
        old_crc32 = RtlComputeCrc32(0, old_file_buf, old_file_size);
        normalize_result = NORMALIZE_RESULT_FAILURE;
        if (old_crc32 != ph.patched_crc32) {

            /* Try again with normalizations applied to the file. */
            normalize_result = normalize_old_file_image(old_file_buf, old_file_size,
                                    ph.flags, NULL, ph.new_image_base, ph.new_image_time,
                                    NULL, 0, NULL, 0);
            if (normalize_result == NORMALIZE_RESULT_FAILURE) {
                err = ERROR_PATCH_CORRUPT;
                goto cleanup;
            } else if (normalize_result >= NORMALIZE_RESULT_SUCCESS_MODIFIED) {
                patched = TRUE;
            }

            old_crc32 = RtlComputeCrc32(0, old_file_buf, old_file_size);
        }

        /* Did we calculate the expected crc32 checksum? */
        if (old_crc32 == ph.patched_crc32) {

            /* Check if patching was necessary. */
            if ((apply_option_flags & APPLY_OPTION_FAIL_IF_CLOSE) != 0
                || ((apply_option_flags & APPLY_OPTION_FAIL_IF_EXACT) != 0 && !patched))
            {
                err = ERROR_PATCH_NOT_NECESSARY;
                goto cleanup;
            }

            if (!(apply_option_flags & APPLY_OPTION_TEST_ONLY) && ph.patched_size) {
                /* Files are identical so copy old to new. */
                memcpy(new_file_buf, old_file_buf, old_file_size);
                found_file = file_info;
            }
        }

        /* Cleanup the retain buffer if expected match was not found. */
        if (!found_file && retain_buffer) {
            VirtualFree(retain_buffer, 0, MEM_RELEASE);
            retain_buffer = NULL;
        }
    }

    /* If the old file didn't match the new file, then check if the old file
     * matches one of the files in the patch file table.
     */
    for (fileno = 0; !found_file && fileno < ph.old_file_count; ++fileno)
    {
        file_info = &ph.file_table[fileno];

        if (file_info->old_size == old_file_size)
        {
            if (patched) {
                memcpy(old_file_buf, old_file_view, old_file_size);
                patched = FALSE;
            }

            if (file_info->retain_range_count && !(apply_option_flags & APPLY_OPTION_TEST_ONLY)) {
                retain_buffer = save_retained_ranges(old_file_buf, old_file_size,
                    file_info->retain_range_array, file_info->retain_range_count, FALSE);
                if (!retain_buffer) {
                    err = GetLastError();
                    goto cleanup;
                }
            }

            normalize_result = normalize_old_file_image(old_file_buf, old_file_size,
                                ph.flags, NULL, ph.new_image_base, ph.new_image_time,
                                file_info->ignore_range_array, file_info->ignore_range_count,
                                file_info->retain_range_array, file_info->retain_range_count);
            if (normalize_result == NORMALIZE_RESULT_FAILURE) {
                err = ERROR_PATCH_CORRUPT;
                goto cleanup;
            } else if (normalize_result >= NORMALIZE_RESULT_SUCCESS_MODIFIED) {
                patched = TRUE;
            }

            old_crc32 = RtlComputeCrc32(0, old_file_buf, old_file_size);
            if (old_crc32 == file_info->old_crc32)
            {
                /* Check if patching is necessary. */
                if (!ph.patched_size) {
                    if (!old_file_size && ((apply_option_flags & APPLY_OPTION_FAIL_IF_CLOSE) != 0
                                        || (apply_option_flags & APPLY_OPTION_FAIL_IF_EXACT) != 0)) {
                        err = ERROR_PATCH_NOT_NECESSARY;
                    }
                    goto cleanup;
                }

                if (file_info->patch_stream_size != 0)
                {
                    /* Check if only testing is necessary. */
                    if (apply_option_flags & APPLY_OPTION_TEST_ONLY) {
                        goto cleanup;
                    }

                    /* Missing lzxd stream means it's a header test extract. */
                    if ((file_info->patch_stream_start + file_info->patch_stream_size) > ph.end) {
                        err = ERROR_PATCH_NOT_AVAILABLE;
                        goto cleanup;
                    }

                    if (file_info->old_size > ((ph.flags & PATCH_OPTION_USE_LZX_LARGE)
                                                ? MAX_LARGE_WINDOW : MAX_NORMAL_WINDOW))
                    {
                        /* interleaved by default but not the same as PATCH_OPTION_INTERLEAVE_FILES
                         * TODO: Add interleaved stream support.
                         */
                        FIXME("interleaved LZXD decompression is not supported.\n");
                        err = ERROR_PATCH_PACKAGE_UNSUPPORTED;
                        goto cleanup;
                    }

                    /* Apply the necessary patches according to the patch transform table. */
                    if (file_info->xfrm_tbl.count) {
                        err = perform_patches_on_old_file_image(&ph.extra_flags, old_file_buf,
                                file_info->old_size, ph.new_res_time, &file_info->xfrm_tbl);
                    }

                    if (err == ERROR_SUCCESS) {

                        /* Decode the compressed patch data.
                         * N.B. This will copy the decoded data into the new file buffer.
                         */
                        err = decode_lzxd_stream(
                            file_info->patch_stream_start, file_info->patch_stream_size,
                            decode_buf, ph.patched_size, file_info->old_size,
                            ph.flags & PATCH_OPTION_USE_LZX_LARGE,
                            progress_fn, progress_ctx);
                    }

                    if (err == ERROR_SUCCESS) {

                        /* Check for expected patched file crc32 checksum.
                         *
                         * N.B. This must be done with the retain and ignore ranges zeroed,
                         *      in other words the retain ranges must be applied after checking
                         *      for the expected checksum.
                         */
                        patched_crc32 = RtlComputeCrc32(0, &decode_buf[file_info->old_size], ph.patched_size);
                        if (patched_crc32 == ph.patched_crc32) {
                            found_file = file_info;
                            break;
                        }
                    }

                } else {

                    /* Check if patching was not necessary. */
                    if (old_file_size == ph.patched_size &&
                        (apply_option_flags & APPLY_OPTION_FAIL_IF_CLOSE) != 0 ) {
                        err = ERROR_PATCH_NOT_NECESSARY;
                        goto cleanup;
                    }

                    /* Chck if only testing is necessary. */
                    if (apply_option_flags & APPLY_OPTION_TEST_ONLY) {
                        goto cleanup;
                    }

                    /* Files should be identical, so copy old to new. */
                    memcpy(new_file_buf, old_file_buf, old_file_size);
                    found_file = file_info;
                    break;
                }
            }
        }

        if (retain_buffer) {
            VirtualFree(retain_buffer, 0, MEM_RELEASE);
            retain_buffer = NULL;
        }
    }

    /* Check if no suitable matching file was found for patching. */
    if (!found_file) {
        err = ERROR_PATCH_WRONG_FILE;
        goto cleanup;
    }

    /* Apply the retained ranges to the new file. */
    apply_retained_ranges(new_file_buf,
        found_file->retain_range_array, found_file->retain_range_count, retain_buffer);

    /* Make final progress callback. */
    if (!progress_callback_wrapper(progress_fn, progress_ctx, 0, ph.patched_size)) {
        err = GetLastError();
        goto cleanup;
    }

    /* Copy the resulting file data to the new file output */
    if (!(apply_option_flags & APPLY_OPTION_TEST_ONLY))
    {
        if (in_new_file_buf == NULL) {
            out_new_file_buf = decode_buf;
            /* caller will VirtualFree the buffer */
            *pnew_file_buf = out_new_file_buf;
        } else {
            out_new_file_buf = in_new_file_buf;
        }
        memmove(out_new_file_buf, new_file_buf, ph.patched_size);
    }

cleanup:
    if (retain_buffer) {
        VirtualFree(retain_buffer, 0, MEM_RELEASE);
    }

    if (decode_buf != NULL && decode_buf != out_new_file_buf) {
        VirtualFree(decode_buf, 0, MEM_RELEASE);
    }

    free_header(&ph);

    return err;
}

BOOL apply_patch_to_file_by_handles(
    HANDLE patch_file_hndl, HANDLE old_file_hndl, HANDLE new_file_hndl,
    ULONG apply_option_flags, PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx)
{
    LARGE_INTEGER patch_size;
    LARGE_INTEGER old_size;
    HANDLE patch_map;
    HANDLE old_map = NULL;
    BYTE *patch_buf;
    const BYTE *old_buf = NULL;
    BYTE *new_buf = NULL;
    ULONG new_size;
    FILETIME new_time;
    BOOL res = FALSE;
    DWORD err = ERROR_SUCCESS;

    /* truncate the output file if required, or set the handle to invalid */
    if (apply_option_flags & APPLY_OPTION_TEST_ONLY)
    {
        new_file_hndl = INVALID_HANDLE_VALUE;
    }
    else if (SetFilePointer(new_file_hndl, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER
        || !SetEndOfFile(new_file_hndl))
    {
        err = GetLastError();
        return FALSE;
    }

    if (patch_file_hndl == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    old_size.QuadPart = 0;
    if (!GetFileSizeEx(patch_file_hndl, &patch_size)
        || (old_file_hndl != INVALID_HANDLE_VALUE && !GetFileSizeEx(old_file_hndl, &old_size)))
    {
        /* Last error set by API */
        return FALSE;
    }

    patch_map = CreateFileMappingW(patch_file_hndl, NULL, PAGE_READONLY, 0, 0, NULL);
    if (patch_map == NULL)
    {
        /* Last error set by API */
        return FALSE;
    }

    if (old_file_hndl != INVALID_HANDLE_VALUE)
    {
        old_map = CreateFileMappingW(old_file_hndl, NULL, PAGE_READONLY, 0, 0, NULL);
        if (old_map == NULL)
        {
            err = GetLastError();
            goto close_patch_map;
        }
    }

    patch_buf = MapViewOfFile(patch_map, FILE_MAP_READ, 0, 0, (SIZE_T)patch_size.QuadPart);
    if (patch_buf == NULL)
    {
        err = GetLastError();
        goto close_old_map;
    }

    if (old_size.QuadPart)
    {
        old_buf = MapViewOfFile(old_map, FILE_MAP_READ, 0, 0, (SIZE_T)old_size.QuadPart);
        if (old_buf == NULL)
        {
            err = GetLastError();
            goto unmap_patch_buf;
        }
    }

    err = apply_patch_to_file_by_buffers(patch_buf, (ULONG)patch_size.QuadPart,
        old_buf, (ULONG)old_size.QuadPart,
        &new_buf, 0, &new_size, &new_time,
        apply_option_flags, progress_fn, progress_ctx);

    if(err)
        goto free_new_buf;

    res = TRUE;

    if(new_file_hndl != INVALID_HANDLE_VALUE)
    {
        DWORD Written = 0;
        res = WriteFile(new_file_hndl, new_buf, new_size, &Written, NULL);

        if (!res)
            err = GetLastError();
        else if (new_time.dwLowDateTime || new_time.dwHighDateTime)
            SetFileTime(new_file_hndl, &new_time, NULL, &new_time);
    }

free_new_buf:
    if (new_buf != NULL)
        VirtualFree(new_buf, 0, MEM_RELEASE);

    if (old_buf != NULL)
        UnmapViewOfFile(old_buf);

unmap_patch_buf:
    UnmapViewOfFile(patch_buf);

close_old_map:
    if (old_map != NULL)
        CloseHandle(old_map);

close_patch_map:
    CloseHandle(patch_map);

    SetLastError(err);

    return res;
}

BOOL apply_patch_to_file(
    LPCWSTR patch_file_name, LPCWSTR old_file_name, LPCWSTR new_file_name,
    const ULONG apply_option_flags, PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx)
{
    HANDLE patch_hndl;
    HANDLE old_hndl = INVALID_HANDLE_VALUE;
    HANDLE new_hndl = INVALID_HANDLE_VALUE;
    BOOL res = FALSE;
    DWORD err = ERROR_SUCCESS;

    patch_hndl = CreateFileW(patch_file_name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (patch_hndl == INVALID_HANDLE_VALUE)
    {
        /* last error set by CreateFileW */
        return FALSE;
    }

    if (old_file_name != NULL)
    {
        old_hndl = CreateFileW(old_file_name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (old_hndl == INVALID_HANDLE_VALUE)
        {
            err = GetLastError();
            goto close_patch_file;
        }
    }

    if (!(apply_option_flags & APPLY_OPTION_TEST_ONLY))
    {
        new_hndl = CreateFileW(new_file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (new_hndl == INVALID_HANDLE_VALUE)
        {
            err = GetLastError();
            goto close_old_file;
        }
    }

    res = apply_patch_to_file_by_handles(patch_hndl, old_hndl, new_hndl, apply_option_flags, progress_fn, progress_ctx);
    if(!res)
        err = GetLastError();

    if (new_hndl != INVALID_HANDLE_VALUE)
    {
        CloseHandle(new_hndl);
        if (!res)
            DeleteFileW(new_file_name);
    }

close_old_file:
    if (old_hndl != INVALID_HANDLE_VALUE)
        CloseHandle(old_hndl);

close_patch_file:
    CloseHandle(patch_hndl);

    /* set last error even on success as per windows */
    SetLastError(err);

    return res;
}
