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
 * - Normalization of 32-bit PE executable files and reversal of special
 *   processing of these executables is not implemented.
 *   Without normalization, old files cannot be validated for patching. The
 *   function NormalizeFileForPatchSignature() in Windows could be used to work
 *   out exactly how normalization works.
 *   Most/all of the special processing seems to be relocation of targets for
 *   some jump/call instructions to match more of the old file and improve
 *   compression. Patching of 64-bit exes works because mspatchc.dll does not
 *   implement special processing of them. In 32-bit patches, the variable
 *   named here 'unknown_count' seems to indicate presence of data related to
 *   reversing the processing. The changes that must be reversed occur at some,
 *   but not all, of the positions listed in the PE .reloc table.
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
#define PATCH_OPTION_EXTRA_FLAGS 0x80000000

static UINT32 compute_zero_crc32(UINT32 crc, INT_PTR len)
{
    static const BYTE zero_buffer[1024];

    while (len)
    {
        crc = RtlComputeCrc32(crc, zero_buffer, min(len, sizeof(zero_buffer)));
        len -= min(len, sizeof(zero_buffer));
    }
    return crc;
}

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


#define MAX_RANGES 255

struct input_file_info {
    size_t input_size;
    DWORD crc32;
    BYTE ignore_range_count;
    BYTE retain_range_count;
    PATCH_IGNORE_RANGE ignore_table[MAX_RANGES];
    PATCH_RETAIN_RANGE retain_table[MAX_RANGES];
    size_t unknown_count;
    size_t stream_size;
    const BYTE *stream_start;
    int next_i;
    int next_r;
};

struct patch_file_header {
    DWORD flags;
    DWORD timestamp;
    size_t patched_size;
    DWORD patched_crc32;
    unsigned input_file_count;
    struct input_file_info *file_table;
    const BYTE *src;
    const BYTE *end;
    DWORD err;
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
    LONG delta = ((PATCH_IGNORE_RANGE*)a)->OffsetInOldFile - ((PATCH_IGNORE_RANGE*)b)->OffsetInOldFile;
    if (delta > 0)
        return 1;
    if (delta < 0)
        return -1;
    return 0;
}

static int __cdecl compare_retained_range_old(const void *a, const void *b)
{
    LONG delta = ((PATCH_RETAIN_RANGE*)a)->OffsetInOldFile - ((PATCH_RETAIN_RANGE*)b)->OffsetInOldFile;
    if (delta > 0)
        return 1;
    if (delta < 0)
        return -1;
    return 0;
}

static int __cdecl compare_retained_range_new(const void *a, const void *b)
{
    LONG delta = ((PATCH_RETAIN_RANGE*)a)->OffsetInNewFile - ((PATCH_RETAIN_RANGE*)b)->OffsetInNewFile;
    if (delta > 0)
        return 1;
    if (delta < 0)
        return -1;
    return 0;
}

static int read_header(struct patch_file_header *ph, const BYTE *buf, size_t size)
{
    unsigned fileno;

    ph->src = buf;
    ph->end = buf + size;

    ph->file_table = NULL;
    ph->err = ERROR_SUCCESS;

    if (read_raw_uint32(ph) != PA19_FILE_MAGIC)
    {
        TRACE("no PA19 signature\n");
        ph->err = ERROR_PATCH_CORRUPT;
        return -1;
    }

    ph->flags = read_raw_uint32(ph);
    if ((ph->flags & PATCH_OPTION_SUPPORTED_FLAGS) != ph->flags)
    {
        FIXME("unsupported option flag(s): 0x%08lx\n", ph->flags & ~PATCH_OPTION_SUPPORTED_FLAGS);
        ph->err = ERROR_PATCH_PACKAGE_UNSUPPORTED;
        return -1;
    }

    /* additional 32-bit flag field */
    if (ph->flags & PATCH_OPTION_EXTRA_FLAGS)
    {
        TRACE("skipping extra flag field\n");
        (void)read_raw_uint32(ph);
    }

    /* the meaning of PATCH_OPTION_NO_TIMESTAMP is inverted for decoding */
    if(ph->flags & PATCH_OPTION_NO_TIMESTAMP)
        ph->timestamp = read_raw_uint32(ph);

    /* not sure what this value is for, but its absence seems to mean only that timestamps
     * in the decompressed 32-bit exe are not modified */
    if (!(ph->flags & PATCH_OPTION_NO_REBASE))
    {
        TRACE("skipping rebase field\n");
        (void)read_uvli(ph);
    }

    ph->patched_size = (size_t)read_uvli(ph);
    TRACE("patched file size will be %u\n", (unsigned)ph->patched_size);
    ph->patched_crc32 = read_raw_uint32(ph);

    ph->input_file_count = *ph->src;
    ++ph->src;
    TRACE("patch supports %u old file(s)\n", ph->input_file_count);
    /* if no old file used, input_file_count is still 1 */
    if (ph->input_file_count == 0)
    {
        ph->err = ERROR_PATCH_CORRUPT;
        return -1;
    }

    if (ph->err != ERROR_SUCCESS)
        return -1;

    ph->file_table = heap_calloc(ph->input_file_count, sizeof(struct input_file_info));
    if (ph->file_table == NULL)
    {
        ph->err = ERROR_OUTOFMEMORY;
        return -1;
    }

    for (fileno = 0; fileno < ph->input_file_count; ++fileno) {
        struct input_file_info *fi = ph->file_table + fileno;
        ptrdiff_t delta;
        unsigned i;

        delta = (ptrdiff_t)read_svli(ph);
        fi->input_size = ph->patched_size + delta;

        fi->crc32 = read_raw_uint32(ph);

        fi->ignore_range_count = *ph->src;
        ++ph->src;
        TRACE("found %u range(s) to ignore\n", fi->ignore_range_count);

        for (i = 0; i < fi->ignore_range_count; ++i) {
            PATCH_IGNORE_RANGE *ir = fi->ignore_table + i;

            ir->OffsetInOldFile = (LONG)read_svli(ph);
            ir->LengthInBytes = (ULONG)read_uvli(ph);

            if (i != 0)
            {
                ir->OffsetInOldFile += fi->ignore_table[i - 1].OffsetInOldFile
                    + fi->ignore_table[i - 1].LengthInBytes;
            }
            if (ir->OffsetInOldFile > fi->input_size
                || ir->OffsetInOldFile + ir->LengthInBytes > fi->input_size
                || ir->LengthInBytes > fi->input_size)
            {
                ph->err = ERROR_PATCH_CORRUPT;
                return -1;
            }
        }

        fi->retain_range_count = *ph->src;
        ++ph->src;
        TRACE("found %u range(s) to retain\n", fi->retain_range_count);

        for (i = 0; i < fi->retain_range_count; ++i) {
            PATCH_RETAIN_RANGE *rr = fi->retain_table + i;

            rr->OffsetInOldFile = (LONG)read_svli(ph);
            if (i != 0)
                rr->OffsetInOldFile +=
                    fi->retain_table[i - 1].OffsetInOldFile + fi->retain_table[i - 1].LengthInBytes;

            rr->OffsetInNewFile = rr->OffsetInOldFile + (LONG)read_svli(ph);
            rr->LengthInBytes = (ULONG)read_uvli(ph);

            if (rr->OffsetInOldFile > fi->input_size
                || rr->OffsetInOldFile + rr->LengthInBytes > fi->input_size
                || rr->OffsetInNewFile > ph->patched_size
                || rr->OffsetInNewFile + rr->LengthInBytes > ph->patched_size
                || rr->LengthInBytes > ph->patched_size)
            {
                ph->err = ERROR_PATCH_CORRUPT;
                return -1;
            }

            /* ranges in new file must be equal and in the same order for all source files */
            if (fileno != 0)
            {
                PATCH_RETAIN_RANGE *rr_0 = ph->file_table[0].retain_table + i;
                if (rr->OffsetInNewFile != rr_0->OffsetInNewFile
                    || rr->LengthInBytes != rr_0->LengthInBytes)
                {
                    ph->err = ERROR_PATCH_CORRUPT;
                    return -1;
                }
            }
        }

        fi->unknown_count = (size_t)read_uvli(ph);
        if (fi->unknown_count)
        {
            FIXME("special processing of 32-bit executables not implemented.\n");
            ph->err = ERROR_PATCH_PACKAGE_UNSUPPORTED;
            return -1;
        }
        fi->stream_size = (size_t)read_uvli(ph);
    }

    for (fileno = 0; fileno < ph->input_file_count; ++fileno)
    {
        struct input_file_info *fi = ph->file_table + fileno;

        qsort(fi->ignore_table, fi->ignore_range_count, sizeof(fi->ignore_table[0]), compare_ignored_range);
        qsort(fi->retain_table, fi->retain_range_count, sizeof(fi->retain_table[0]), compare_retained_range_old);

        fi->stream_start = ph->src;
        ph->src += fi->stream_size;
    }

    /* skip the crc adjustment field */
    ph->src = min(ph->src + 4, ph->end);

    {
        UINT32 crc = RtlComputeCrc32(0, buf, ph->src - buf) ^ 0xFFFFFFFF;
        if (crc != 0)
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
    heap_free(ph->file_table);
}

#define TICKS_PER_SEC 10000000
#define SEC_TO_UNIX_EPOCH ((369 * 365 + 89) * (ULONGLONG)86400)

static void posix_time_to_file_time(ULONG timestamp, FILETIME *ft)
{
    UINT64 ticks = ((UINT64)timestamp + SEC_TO_UNIX_EPOCH) * TICKS_PER_SEC;
    ft->dwLowDateTime = (DWORD)ticks;
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
}

/* Get the next range to ignore in the old file.
 * fi->next_i must be initialized before use */
static ULONG next_ignored_range(const struct input_file_info *fi, size_t index, ULONG old_file_size, ULONG *end)
{
    ULONG start = old_file_size;
    *end = old_file_size;
    /* if patching is unnecessary, the ignored ranges are skipped during crc calc */
    if (fi->next_i < fi->ignore_range_count && fi->stream_size != 0)
    {
        start = fi->ignore_table[fi->next_i].OffsetInOldFile;
        *end = max(start + fi->ignore_table[fi->next_i].LengthInBytes, index);
        start = max(start, index);
    }
    return start;
}

/* Get the next range to retain from the old file.
 * fi->next_r must be initialized before use */
static ULONG next_retained_range_old(const struct input_file_info *fi, size_t index, ULONG old_file_size, ULONG *end)
{
    ULONG start = old_file_size;
    *end = old_file_size;
    if (fi->next_r < fi->retain_range_count)
    {
        start = fi->retain_table[fi->next_r].OffsetInOldFile;
        *end = max(start + fi->retain_table[fi->next_r].LengthInBytes, index);
        start = max(start, index);
    }
    return start;
}

/* Get the next range to retain in the new file.
 * fi->next_r must be initialized before use */
static ULONG next_retained_range_new(const struct input_file_info *fi, size_t index, ULONG new_file_size, ULONG *end)
{
    ULONG start = new_file_size;
    *end = new_file_size;
    if (fi->next_r < fi->retain_range_count)
    {
        start = fi->retain_table[fi->next_r].OffsetInNewFile;
        *end = max(start + fi->retain_table[fi->next_r].LengthInBytes, index);
        start = max(start, index);
    }
    return start;
}

/* Find the next range in the old file which must be assumed zero-filled during crc32 calc
 */
static ULONG next_zeroed_range(struct input_file_info *fi, size_t index, ULONG old_file_size, ULONG *end)
{
    ULONG start = old_file_size;
    ULONG end_i;
    ULONG start_i;
    ULONG end_r;
    ULONG start_r;

    *end = old_file_size;

    start_i = next_ignored_range(fi, index, old_file_size, &end_i);
    start_r = next_retained_range_old(fi, index, old_file_size, &end_r);

    if (start_i < start_r)
    {
        start = start_i;
        *end = end_i;
        ++fi->next_i;
    }
    else
    {
        start = start_r;
        *end = end_r;
        ++fi->next_r;
    }
    return start;
}

/* Use the crc32 of the input file to match the file with an entry in the patch file table
 */
struct input_file_info *find_matching_old_file(const struct patch_file_header *ph, const BYTE *old_file_view, ULONG old_file_size)
{
    unsigned i;

    for (i = 0; i < ph->input_file_count; ++i)
    {
        DWORD crc32 = 0;
        ULONG index;

        if (ph->file_table[i].input_size != old_file_size)
            continue;

        ph->file_table[i].next_i = 0;
        for (index = 0; index < old_file_size; )
        {
            ULONG end;
            ULONG start = next_zeroed_range(ph->file_table + i, index, old_file_size, &end);
            crc32 = RtlComputeCrc32(crc32, old_file_view + index, start - index);
            crc32 = compute_zero_crc32(crc32, end - start);
            index = end;
        }
        if (ph->file_table[i].crc32 == crc32)
            return ph->file_table + i;
    }
    return NULL;
}

/* Zero-fill ignored ranges in the old file data for decoder matching
 */
static void zero_fill_ignored_ranges(BYTE *old_file_buf, const struct input_file_info *fi)
{
    size_t i;
    for (i = 0; i < fi->ignore_range_count; ++i)
    {
        memset(old_file_buf + fi->ignore_table[i].OffsetInOldFile,
            0,
            fi->ignore_table[i].LengthInBytes);
    }
}

/* Zero-fill retained ranges in the old file data for decoder matching
 */
static void zero_fill_retained_ranges(BYTE *old_file_buf, BYTE *new_file_buf, const struct input_file_info *fi)
{
    size_t i;
    for (i = 0; i < fi->retain_range_count; ++i)
    {
        memset(old_file_buf + fi->retain_table[i].OffsetInOldFile,
            0,
            fi->retain_table[i].LengthInBytes);
    }
}

/* Copy the retained ranges to the new file buffer
 */
static void apply_retained_ranges(const BYTE *old_file_buf, BYTE *new_file_buf, const struct input_file_info *fi)
{
    size_t i;

    if (old_file_buf == NULL)
        return;

    for (i = 0; i < fi->retain_range_count; ++i)
    {
        memcpy(new_file_buf + fi->retain_table[i].OffsetInNewFile,
            old_file_buf + fi->retain_table[i].OffsetInOldFile,
            fi->retain_table[i].LengthInBytes);
    }
}

/* Compute the crc32 for the new file, assuming zero for the retained ranges
 */
static DWORD compute_target_crc32(struct input_file_info *fi, const BYTE *new_file_buf, ULONG new_file_size)
{
    DWORD crc32 = 0;
    ULONG index;

    qsort(fi->retain_table, fi->retain_range_count, sizeof(fi->retain_table[0]), compare_retained_range_new);
    fi->next_r = 0;

    for (index = 0; index < new_file_size; )
    {
        ULONG end;
        ULONG start = next_retained_range_new(fi, index, new_file_size, &end);
        ++fi->next_r;
        crc32 = RtlComputeCrc32(crc32, new_file_buf + index, start - index);
        crc32 = compute_zero_crc32(crc32, end - start);
        index = end;
    }
    return crc32;
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
        || ((PUCHAR)reloc_block > (mapped_image_end - sizeof(IMAGE_BASE_RELOCATION)))) {
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

DWORD apply_patch_to_file_by_buffers(const BYTE *patch_file_view, const ULONG patch_file_size,
    const BYTE *old_file_view, ULONG old_file_size,
    BYTE **pnew_file_buf, const ULONG new_file_buf_size, ULONG *new_file_size,
    FILETIME *new_file_time,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only)
{
    DWORD err = ERROR_SUCCESS;
    struct input_file_info *file_info;
    struct patch_file_header ph;
    size_t buf_size;
    BYTE *new_file_buf = NULL;
    BYTE *decode_buf = NULL;

    if (pnew_file_buf == NULL)
    {
        if (!test_header_only && !(apply_option_flags & APPLY_OPTION_TEST_ONLY))
            return ERROR_INVALID_PARAMETER;
    }
    else
    {
        new_file_buf = *pnew_file_buf;
    }

    if (old_file_view == NULL)
        old_file_size = 0;

    if (read_header(&ph, patch_file_view, patch_file_size))
    {
        err = ph.err;
        goto free_patch_header;
    }

    if (new_file_size != NULL)
        *new_file_size = (ULONG)ph.patched_size;

    if (new_file_buf != NULL && new_file_buf_size < ph.patched_size)
    {
        err = ERROR_INSUFFICIENT_BUFFER;
        goto free_patch_header;
    }

    file_info = find_matching_old_file(&ph, old_file_view, old_file_size);
    if (file_info == NULL)
    {
        err = ERROR_PATCH_WRONG_FILE;
        goto free_patch_header;
    }
    if (file_info->input_size != old_file_size)
    {
        err = ERROR_PATCH_CORRUPT;
        goto free_patch_header;
    }
    if (file_info->stream_size == 0 && (apply_option_flags & APPLY_OPTION_FAIL_IF_EXACT))
    {
        err = ERROR_PATCH_NOT_NECESSARY;
        goto free_patch_header;
    }
    if (file_info->stream_size != 0
        && file_info->input_size > ((ph.flags & PATCH_OPTION_USE_LZX_LARGE) ? MAX_LARGE_WINDOW : MAX_NORMAL_WINDOW))
    {
        /* interleaved by default but not the same as PATCH_OPTION_INTERLEAVE_FILES */
        FIXME("interleaved LZXD decompression is not supported.\n");
        err = ERROR_PATCH_PACKAGE_UNSUPPORTED;
        goto free_patch_header;
    }

    if (test_header_only)
        goto free_patch_header;

    /* missing lzxd stream means it's a header test extract */
    if (file_info->stream_start + file_info->stream_size > ph.end)
    {
        err = ERROR_PATCH_NOT_AVAILABLE;
        goto free_patch_header;
    }

    buf_size = old_file_size + ph.patched_size;
    decode_buf = new_file_buf;
    if (new_file_buf == NULL || new_file_buf_size < buf_size)
    {
        /* decode_buf must have room for both files, so allocate a new buffer if
         * necessary. This will be returned to the caller if new_file_buf == NULL */
        decode_buf = VirtualAlloc(NULL, buf_size, MEM_COMMIT, PAGE_READWRITE);
        if (decode_buf == NULL)
        {
            err = GetLastError();
            goto free_patch_header;
        }
    }

    if (old_file_view != NULL)
        memcpy(decode_buf, old_file_view, file_info->input_size);

    zero_fill_ignored_ranges(decode_buf, file_info);
    zero_fill_retained_ranges(decode_buf, decode_buf + file_info->input_size, file_info);

    if (file_info->stream_size != 0)
    {
        err = decode_lzxd_stream(file_info->stream_start, file_info->stream_size,
            decode_buf, ph.patched_size, file_info->input_size,
            ph.flags & PATCH_OPTION_USE_LZX_LARGE,
            progress_fn, progress_ctx);
    }
    else if (file_info->input_size == ph.patched_size)
    {
        /* files are identical so copy old to new. copying is avoidable but rare */
        memcpy(decode_buf + file_info->input_size, decode_buf, ph.patched_size);
    }
    else
    {
        err = ERROR_PATCH_CORRUPT;
        goto free_decode_buf;
    }

    if(err != ERROR_SUCCESS)
    {
        if (err == ERROR_PATCH_DECODE_FAILURE)
            FIXME("decode failure: data corruption or bug.\n");
        goto free_decode_buf;
    }

    apply_retained_ranges(old_file_view, decode_buf + file_info->input_size, file_info);

    if (ph.patched_crc32 != compute_target_crc32(file_info, decode_buf + file_info->input_size, ph.patched_size))
    {
        err = ERROR_PATCH_CORRUPT;
        goto free_decode_buf;
    }

    /* retained ranges must be ignored for this test */
    if ((apply_option_flags & APPLY_OPTION_FAIL_IF_EXACT)
        && file_info->input_size == ph.patched_size
        && memcmp(decode_buf, decode_buf + file_info->input_size, ph.patched_size) == 0)
    {
        err = ERROR_PATCH_NOT_NECESSARY;
        goto free_decode_buf;
    }

    if (!(apply_option_flags & APPLY_OPTION_TEST_ONLY))
    {
        if (new_file_buf == NULL)
        {
            /* caller will VirtualFree the buffer */
            new_file_buf = decode_buf;
            *pnew_file_buf = new_file_buf;
        }
        memmove(new_file_buf, decode_buf + old_file_size, ph.patched_size);
    }

    if (new_file_time != NULL)
    {
        new_file_time->dwLowDateTime = 0;
        new_file_time->dwHighDateTime = 0;

        /* the meaning of PATCH_OPTION_NO_TIMESTAMP is inverted for decoding */
        if (ph.flags & PATCH_OPTION_NO_TIMESTAMP)
            posix_time_to_file_time(ph.timestamp, new_file_time);
    }

free_decode_buf:
    if(decode_buf != NULL && decode_buf != new_file_buf)
        VirtualFree(decode_buf, 0, MEM_RELEASE);

free_patch_header:
    free_header(&ph);

    return err;
}

BOOL apply_patch_to_file_by_handles(HANDLE patch_file_hndl, HANDLE old_file_hndl, HANDLE new_file_hndl,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only)
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
    if (test_header_only || (apply_option_flags & APPLY_OPTION_TEST_ONLY))
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
        &new_buf, 0, &new_size,
        &new_time,
        apply_option_flags, progress_fn, progress_ctx,
        test_header_only);

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

BOOL apply_patch_to_file(LPCWSTR patch_file_name, LPCWSTR old_file_name, LPCWSTR new_file_name,
    const ULONG apply_option_flags,
    PATCH_PROGRESS_CALLBACK *progress_fn, void *progress_ctx,
    const BOOL test_header_only)
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

    if (!test_header_only && !(apply_option_flags & APPLY_OPTION_TEST_ONLY))
    {
        new_hndl = CreateFileW(new_file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (new_hndl == INVALID_HANDLE_VALUE)
        {
            err = GetLastError();
            goto close_old_file;
        }
    }

    res = apply_patch_to_file_by_handles(patch_hndl, old_hndl, new_hndl, apply_option_flags, progress_fn, progress_ctx, test_header_only);
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
