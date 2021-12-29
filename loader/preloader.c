/*
 * Preloader for ld.so
 *
 * Copyright (C) 1995,96,97,98,99,2000,2001,2002 Free Software Foundation, Inc.
 * Copyright (C) 2004 Mike McCormack for CodeWeavers
 * Copyright (C) 2004 Alexandre Julliard
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

/*
 * Design notes
 *
 * The goal of this program is to be a workaround for exec-shield, as used
 *  by the Linux kernel distributed with Fedora Core and other distros.
 *
 * To do this, we implement our own shared object loader that reserves memory
 * that is important to Wine, and then loads the main binary and its ELF
 * interpreter.
 *
 * We will try to set up the stack and memory area so that the program that
 * loads after us (eg. the wine binary) never knows we were here, except that
 * areas of memory it needs are already magically reserved.
 *
 * The following memory areas are important to Wine:
 *  0x00000000 - 0x00110000  the DOS area
 *  0x80000000 - 0x81000000  the shared heap
 *  ???        - ???         the PE binary load address (usually starting at 0x00400000)
 *
 * If this program is used as the shared object loader, the only difference
 * that the loaded programs should see is that this loader will be mapped
 * into memory when it starts.
 */

/*
 * References (things I consulted to understand how ELF loading works):
 *
 * glibc 2.3.2   elf/dl-load.c
 *  http://www.gnu.org/directory/glibc.html
 *
 * Linux 2.6.4   fs/binfmt_elf.c
 *  ftp://ftp.kernel.org/pub/linux/kernel/v2.6/linux-2.6.4.tar.bz2
 *
 * Userland exec, by <grugq@hcunix.net>
 *  http://cert.uni-stuttgart.de/archive/bugtraq/2004/01/msg00002.html
 *
 * The ELF specification:
 *  http://www.linuxbase.org/spec/booksets/LSB-Embedded/LSB-Embedded/book387.html
 */

#ifdef __linux__

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
#endif
#include <unistd.h>
#ifdef HAVE_ELF_H
# include <elf.h>
#endif
#ifdef HAVE_LINK_H
# include <link.h>
#endif
#ifdef HAVE_SYS_LINK_H
# include <sys/link.h>
#endif
#ifdef HAVE_SYS_UCONTEXT_H
# include <sys/ucontext.h>
#endif

#include "wine/asm.h"
#include "main.h"

/* ELF definitions */
#define ELF_PREFERRED_ADDRESS(loader, maplength, mapstartpref) (mapstartpref)
#define ELF_FIXED_ADDRESS(loader, mapstart) ((void) 0)

#define MAP_BASE_ADDR(l)     0

#ifndef MAP_COPY
#define MAP_COPY MAP_PRIVATE
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif

#define REMAP_TEST_SIG SIGIO  /* Any signal GDB doesn't stop on */

static struct wine_preload_info preload_info[] =
{
#if defined(__i386__) || defined(__arm__)
    { (void *)0x00000000, 0x00010000 },  /* low 64k */
    { (void *)0x00010000, 0x00100000 },  /* DOS area */
    { (void *)0x00110000, 0x67ef0000 },  /* low memory area */
    { (void *)0x7f000000, 0x03000000 },  /* top-down allocations + shared heap + virtual heap */
#else
    { (void *)0x000000010000, 0x00100000 },  /* DOS area */
    { (void *)0x000000110000, 0x67ef0000 },  /* low memory area */
    { (void *)0x00007ff00000, 0x000f0000 },  /* shared user data */
    { (void *)0x7ffffe000000, 0x01ff0000 },  /* top-down allocations + virtual heap */
#endif
    { 0, 0 },                            /* PE exe range set with WINEPRELOADRESERVE */
    { 0, 0 }                             /* end of list */
};

/* debugging */
#undef DUMP_SEGMENTS
#undef DUMP_AUX_INFO
#undef DUMP_SYMS
#undef DUMP_MAPS

/* older systems may not define these */
#ifndef PT_TLS
#define PT_TLS 7
#endif

#ifndef AT_SYSINFO
#define AT_SYSINFO 32
#endif
#ifndef AT_SYSINFO_EHDR
#define AT_SYSINFO_EHDR 33
#endif

#ifndef DT_GNU_HASH
#define DT_GNU_HASH 0x6ffffef5
#endif

static size_t page_size, page_mask;
static char *preloader_start, *preloader_end;

struct wld_link_map {
    ElfW(Addr) l_addr;
    ElfW(Dyn) *l_ld;
    ElfW(Phdr)*l_phdr;
    ElfW(Addr) l_entry;
    ElfW(Half) l_ldnum;
    ElfW(Half) l_phnum;
    ElfW(Addr) l_map_start, l_map_end;
    ElfW(Addr) l_interp;
};

struct wld_auxv
{
    ElfW(Addr) a_type;
    union
    {
        ElfW(Addr) a_val;
    } a_un;
};

typedef unsigned long wld_sigset_t[8 / sizeof(unsigned long)];

struct wld_sigaction
{
    /* Prefix all fields since they may collide with macros from libc headers */
    void (*wld_sa_sigaction)(int, siginfo_t *, void *);
    unsigned long wld_sa_flags;
    void (*wld_sa_restorer)(void);
    wld_sigset_t wld_sa_mask;
};

#define WLD_SA_SIGINFO 4

/* Aggregates information about initial program stack and variables
 * (e.g. argv and envp) that reside in it.
 */
struct stackarg_info
{
    void *stack;
    int argc;
    char **argv;
    char **envp;
    struct wld_auxv *auxv;
    struct wld_auxv *auxv_end;
};

/* Currently only contains the main stackarg_info. */
struct preloader_state
{
    struct stackarg_info s;
};

/* Buffer for line-buffered I/O read. */
struct linebuffer
{
    char *base;     /* start of the buffer */
    char *limit;    /* last byte of the buffer (for NULL terminator) */
    char *head;     /* next byte to write to */
    char *tail;     /* next byte to read from */
    int truncated;  /* line truncated? (if true, skip until next line) */
};

/*
 * Flags that specify the kind of each VMA entry read from /proc/self/maps.
 *
 * On Linux, vDSO hard-codes vvar's address relative to vDSO.  Therefore, it is
 * necessary to maintain vvar's position relative to vDSO when they are
 * remapped.  We cannot just remap one of them and leave the other one behind;
 * they have to be moved as a single unit.  Doing so requires identifying the
 * *exact* size and boundaries of *both* mappings.  This is met by a few
 * challenges:
 *
 * 1. vvar's size *and* its location relative to vDSO is *not* guaranteed by
 *    Linux userspace ABI, and has changed all the time.
 *
 *    - x86: [vvar] orginally resided at a fixed address 0xffffffffff5ff000
 *      (64-bit) [1], but was later changed so that it precedes [vdso] [2].
 *      There, sym_vvar_start is a negative value [3]. text_start is the base
 *      address of vDSO, and addr becomes the address of vvar.
 *
 *    - AArch32: [vvar] is a single page and precedes [vdso] [4].
 *
 *    - AArch64: [vvar] is two pages long and precedes [vdso] [5].
 *      Before v5.9, however, [vvar] was a single page [6].
 *
 * 2. It's very difficult to infer vDSO and vvar's size and offset relative to
 *    each other just from vDSO data.  Since vvar's symbol does not exist in
 *    vDSO's symtab, determining the layout would require parsing vDSO's code.
 *
 * 3. Determining the size of both mappings is not a trivial task.  Even if we
 *    parse vDSO's ELF header, we cannot still measure the size of vvar.
 *
 * Therefore, the only reliable method to identify the range of the mappings is
 * to read from /proc/self/maps.  This is also what the CRIU (Checkpoint
 * Restore In Userspace) project uses for relocating vDSO [7].
 *
 * [1] https://lwn.net/Articles/615809/
 * [2] https://elixir.bootlin.com/linux/v5.16.3/source/arch/x86/entry/vdso/vma.c#L246
 * [3] https://elixir.bootlin.com/linux/v5.16.3/source/arch/x86/include/asm/vdso.h#L21
 * [4] https://elixir.bootlin.com/linux/v5.16.3/source/arch/arm/kernel/vdso.c#L236
 * [5] https://elixir.bootlin.com/linux/v5.16.3/source/arch/arm64/kernel/vdso.c#L214
 * [6] https://elixir.bootlin.com/linux/v5.8/source/arch/arm64/kernel/vdso.c#L161
 * [7] https://github.com/checkpoint-restore/criu/blob/2f0f12839673c7d82cfc18e99d7e35ed7192906b/criu/vdso.c
 */
enum vma_type_flags
{
    VMA_NORMAL = 0x01,
    VMA_VDSO   = 0x02,
    VMA_VVAR   = 0x04,
};

struct vma_area
{
    unsigned long start;
    unsigned long end;
    unsigned char type_flags;  /* enum vma_type_flags */
    unsigned char moved;       /* has been mremap()'d? */
};

struct vma_area_list
{
    struct vma_area *base;
    struct vma_area *list_end;
    struct vma_area *alloc_end;
};

#define FOREACH_VMA(list, item) \
    for ((item) = (list)->base; (item) != (list)->list_end; (item)++)

/*
 * Allow the user to configure the remapping behaviour if it causes trouble.
 * The "force" (REMAP_POLICY_FORCE) value can be used to test the remapping
 * code path unconditionally.
 */
enum remap_policy
{
    REMAP_POLICY_ON_CONFLICT = 0,
    REMAP_POLICY_FORCE = 1,
    REMAP_POLICY_SKIP = 2,
    LAST_REMAP_POLICY,

    REMAP_POLICY_DEFAULT_VDSO = REMAP_POLICY_SKIP,
};

/*
 * Used in the signal handler that tests if mremap() on vDSO works on the
 * current kernel.
 */
struct remap_test_block
{
    /*
     * The old address range of vDSO or sigpage.  Used to test if pages are
     * remapped properly.
     */
    unsigned long old_mapping_start;
    unsigned long old_mapping_size;

    /*
     * A snapshot of the VMA area list of the current process.  Used to restore
     * vDSO mappings on remapping failure from the signal handler.
     */
    struct vma_area_list *vma_list;

    /*
     * The difference between the new mapping's address and the old mapping's
     * address.  Set to 0 if the handler reverted mappings to old state before
     * returning.
     */
    unsigned long delta;

    /*
     * Set to 1 by the signal handler if it determines that the remapping was
     * successfully recognised by the kernel.
     */
    unsigned char is_successful;

    /*
     * Set to 1 by the signal handler if it determines that the remapping was
     * not recognised by the kernel.
     */
    unsigned char is_failed;
} remap_test;

/*
 * The __bb_init_func is an empty function only called when file is
 * compiled with gcc flags "-fprofile-arcs -ftest-coverage".  This
 * function is normally provided by libc's startup files, but since we
 * build the preloader with "-nostartfiles -nodefaultlibs", we have to
 * provide our own (empty) version, otherwise linker fails.
 */
void __bb_init_func(void) { return; }

/* similar to the above but for -fstack-protector */
void *__stack_chk_guard = 0;
void __stack_chk_fail_local(void) { return; }
void __stack_chk_fail(void) { return; }

#ifdef __i386__

/* data for setting up the glibc-style thread-local storage in %gs */

static int thread_data[256];

struct
{
    /* this is the kernel modify_ldt struct */
    unsigned int  entry_number;
    unsigned long base_addr;
    unsigned int  limit;
    unsigned int  seg_32bit : 1;
    unsigned int  contents : 2;
    unsigned int  read_exec_only : 1;
    unsigned int  limit_in_pages : 1;
    unsigned int  seg_not_present : 1;
    unsigned int  usable : 1;
    unsigned int  garbage : 25;
} thread_ldt = { -1, (unsigned long)thread_data, 0xfffff, 1, 0, 0, 1, 0, 1, 0 };

typedef unsigned long wld_old_sigset_t;

struct wld_old_sigaction
{
    /* Prefix all fields since they may collide with macros from libc headers */
    void (*wld_sa_sigaction)(int, siginfo_t *, void *);
    wld_old_sigset_t wld_sa_mask;
    unsigned long wld_sa_flags;
    void (*wld_sa_restorer)(void);
};

/*
 * The _start function is the entry and exit point of this program
 *
 *  It calls wld_start, passing a pointer to the args it receives
 *  then jumps to the address wld_start returns.
 */
void _start(void);
extern char __executable_start[];
extern char _end[];
__ASM_GLOBAL_FUNC(_start,
                  __ASM_CFI("\t.cfi_undefined %eip\n")
                  "\tmovl $243,%eax\n"        /* SYS_set_thread_area */
                  "\tmovl $thread_ldt,%ebx\n"
                  "\tint $0x80\n"             /* allocate gs segment */
                  "\torl %eax,%eax\n"
                  "\tjl 1f\n"
                  "\tmovl thread_ldt,%eax\n"  /* thread_ldt.entry_number */
                  "\tshl $3,%eax\n"
                  "\torl $3,%eax\n"
                  "\tmov %ax,%gs\n"
                  "\tmov %ax,%fs\n"           /* set %fs too so libwine can retrieve it later on */
                  "1:\tmovl %esp,%eax\n"
                  "\tleal -136(%esp),%esp\n"  /* allocate some space for extra aux values */
                  "\tpushl %eax\n"            /* orig stack pointer */
                  "\tpushl %esp\n"            /* ptr to orig stack pointer */
                  "\tcall wld_start\n"
                  "\tpopl %ecx\n"             /* remove ptr to stack pointer */
                  "\tpopl %esp\n"             /* new stack pointer */
                  "\tpush %eax\n"             /* ELF interpreter entry point */
                  "\txor %eax,%eax\n"
                  "\txor %ecx,%ecx\n"
                  "\txor %edx,%edx\n"
                  "\tmov %ax,%gs\n"           /* clear %gs again */
                  "\tret\n")

/* wrappers for Linux system calls */

#define SYSCALL_RET(ret) (((ret) < 0 && (ret) > -4096) ? -1 : (ret))

static inline __attribute__((noreturn)) void wld_exit( int code )
{
    for (;;)  /* avoid warning */
        __asm__ __volatile__( "pushl %%ebx; movl %1,%%ebx; int $0x80; popl %%ebx"
                              : : "a" (1 /* SYS_exit */), "r" (code) );
}

static inline int wld_open( const char *name, int flags )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (5 /* SYS_open */), "r" (name), "c" (flags) );
    return SYSCALL_RET(ret);
}

static inline int wld_close( int fd )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (6 /* SYS_close */), "r" (fd) );
    return SYSCALL_RET(ret);
}

static inline ssize_t wld_read( int fd, void *buffer, size_t len )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret)
                          : "0" (3 /* SYS_read */), "r" (fd), "c" (buffer), "d" (len)
                          : "memory" );
    return SYSCALL_RET(ret);
}

static inline ssize_t wld_write( int fd, const void *buffer, size_t len )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (4 /* SYS_write */), "r" (fd), "c" (buffer), "d" (len) );
    return SYSCALL_RET(ret);
}

static inline int wld_mprotect( const void *addr, size_t len, int prot )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (125 /* SYS_mprotect */), "r" (addr), "c" (len), "d" (prot) );
    return SYSCALL_RET(ret);
}

void *wld_mmap( void *start, size_t len, int prot, int flags, int fd, unsigned int offset );
__ASM_GLOBAL_FUNC(wld_mmap,
                  "\tpushl %ebp\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
                  "\tpushl %ebx\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
                  "\tpushl %esi\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
                  "\tpushl %edi\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
                  "\tmovl $192,%eax\n"      /* SYS_mmap2 */
                  "\tmovl 20(%esp),%ebx\n"  /* start */
                  "\tmovl 24(%esp),%ecx\n"  /* len */
                  "\tmovl 28(%esp),%edx\n"  /* prot */
                  "\tmovl 32(%esp),%esi\n"  /* flags */
                  "\tmovl 36(%esp),%edi\n"  /* fd */
                  "\tmovl 40(%esp),%ebp\n"  /* offset */
                  "\tshrl $12,%ebp\n"
                  "\tint $0x80\n"
                  "\tcmpl $-4096,%eax\n"
                  "\tjbe 2f\n"
                  "\tcmpl $-38,%eax\n"      /* ENOSYS */
                  "\tjne 1f\n"
                  "\tmovl $90,%eax\n"       /* SYS_mmap */
                  "\tleal 20(%esp),%ebx\n"
                  "\tint $0x80\n"
                  "\tcmpl $-4096,%eax\n"
                  "\tjbe 2f\n"
                  "1:\tmovl $-1,%eax\n"
                  "2:\tpopl %edi\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset -4\n\t")
                  "\tpopl %esi\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset -4\n\t")
                  "\tpopl %ebx\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset -4\n\t")
                  "\tpopl %ebp\n"
                  __ASM_CFI(".cfi_adjust_cfa_offset -4\n\t")
                  "\tret\n" )

static inline int wld_munmap( void *addr, size_t len )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (91 /* SYS_munmap */), "r" (addr), "c" (len)
                          : "memory" );
    return SYSCALL_RET(ret);
}

static inline void *wld_mremap( void *old_addr, size_t old_len, size_t new_size, int flags, void *new_addr )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (163 /* SYS_mremap */), "r" (old_addr), "c" (old_len),
                            "d" (new_size), "S" (flags), "D" (new_addr)
                          : "memory" );
    return (void *)SYSCALL_RET(ret);
}

static inline int wld_prctl( int code, long arg )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (172 /* SYS_prctl */), "r" (code), "c" (arg) );
    return SYSCALL_RET(ret);
}

static void copy_old_sigset( void *dest, const void *src )
{
    /* Avoid aliasing */
    size_t i;
    for (i = 0; i < sizeof(wld_old_sigset_t); i++)
        *((unsigned char *)dest + i) = *((const unsigned char *)src + i);
}

static inline int wld_sigaction( int signum, const struct wld_sigaction *act, struct wld_sigaction *old_act )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (174 /* SYS_rt_sigaction */), "r" (signum), "c" (act), "d" (old_act), "S" (sizeof(act->wld_sa_mask))
                          : "memory" );
    if (ret == -38 /* ENOSYS */)
    {
        struct wld_old_sigaction act_buf, old_act_buf, *act_real, *old_act_real;

        if (act)
        {
            act_real = &act_buf;
            act_buf.wld_sa_sigaction = act->wld_sa_sigaction;
            copy_old_sigset(&act_buf.wld_sa_mask, &act->wld_sa_mask);
            act_buf.wld_sa_flags = act->wld_sa_flags;
            act_buf.wld_sa_restorer = act->wld_sa_restorer;
        }

        if (old_act) old_act_real = &old_act_buf;

        __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                              : "=a" (ret) : "0" (67 /* SYS_sigaction */), "r" (signum), "c" (act_real), "d" (old_act_real)
                              : "memory" );

        if (old_act && ret >= 0)
        {
            old_act->wld_sa_sigaction = old_act_buf.wld_sa_sigaction;
            old_act->wld_sa_flags = old_act_buf.wld_sa_flags;
            old_act->wld_sa_restorer = old_act_buf.wld_sa_restorer;
            copy_old_sigset(&old_act->wld_sa_mask, &old_act_buf.wld_sa_mask);
        }
    }
    return SYSCALL_RET(ret);
}

static inline int wld_kill( pid_t pid, int sig )
{
    int ret;
    __asm__ __volatile__( "pushl %%ebx; movl %2,%%ebx; int $0x80; popl %%ebx"
                          : "=a" (ret) : "0" (37 /* SYS_kill */), "r" (pid), "c" (sig)
                          : "memory" /* clobber: signal handler side effects on raise() */ );
    return SYSCALL_RET(ret);
}

static inline pid_t wld_getpid( void )
{
    int ret;
    __asm__ __volatile__( "int $0x80"
                          : "=a" (ret) : "0" (20 /* SYS_getpid */) );
    return ret;
}

#elif defined(__x86_64__)

void *thread_data[256];

/*
 * The _start function is the entry and exit point of this program
 *
 *  It calls wld_start, passing a pointer to the args it receives
 *  then jumps to the address wld_start returns.
 */
void _start(void);
extern char __executable_start[];
extern char _end[];
__ASM_GLOBAL_FUNC(_start,
                  __ASM_CFI(".cfi_undefined %rip\n\t")
                  "movq %rsp,%rax\n\t"
                  "leaq -144(%rsp),%rsp\n\t" /* allocate some space for extra aux values */
                  "movq %rax,(%rsp)\n\t"     /* orig stack pointer */
                  "movq $thread_data,%rsi\n\t"
                  "movq $0x1002,%rdi\n\t"    /* ARCH_SET_FS */
                  "movq $158,%rax\n\t"       /* SYS_arch_prctl */
                  "syscall\n\t"
                  "movq %rsp,%rdi\n\t"       /* ptr to orig stack pointer */
                  "call wld_start\n\t"
                  "movq (%rsp),%rsp\n\t"     /* new stack pointer */
                  "pushq %rax\n\t"           /* ELF interpreter entry point */
                  "xorq %rax,%rax\n\t"
                  "xorq %rcx,%rcx\n\t"
                  "xorq %rdx,%rdx\n\t"
                  "xorq %rsi,%rsi\n\t"
                  "xorq %rdi,%rdi\n\t"
                  "xorq %r8,%r8\n\t"
                  "xorq %r9,%r9\n\t"
                  "xorq %r10,%r10\n\t"
                  "xorq %r11,%r11\n\t"
                  "ret")

#define SYSCALL_FUNC( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "movq $" #nr ",%rax\n\t" \
                       "movq %rcx,%r10\n\t" \
                       "syscall\n\t" \
                       "leaq 4096(%rax),%rcx\n\t" \
                       "movq $-1,%rdx\n\t" \
                       "cmp $4096,%rcx\n\t" \
                       "cmovb %rdx,%rax\n\t" \
                       "ret" )

#define SYSCALL_NOERR( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "movq $" #nr ",%rax\n\t" \
                       "syscall\n\t" \
                       "ret" )

void wld_exit( int code ) __attribute__((noreturn));
SYSCALL_NOERR( wld_exit, 60 /* SYS_exit */ );

ssize_t wld_read( int fd, void *buffer, size_t len );
SYSCALL_FUNC( wld_read, 0 /* SYS_read */ );

ssize_t wld_write( int fd, const void *buffer, size_t len );
SYSCALL_FUNC( wld_write, 1 /* SYS_write */ );

int wld_open( const char *name, int flags );
SYSCALL_FUNC( wld_open, 2 /* SYS_open */ );

int wld_close( int fd );
SYSCALL_FUNC( wld_close, 3 /* SYS_close */ );

void *wld_mmap( void *start, size_t len, int prot, int flags, int fd, off_t offset );
SYSCALL_FUNC( wld_mmap, 9 /* SYS_mmap */ );

int wld_mprotect( const void *addr, size_t len, int prot );
SYSCALL_FUNC( wld_mprotect, 10 /* SYS_mprotect */ );

int wld_munmap( void *addr, size_t len );
SYSCALL_FUNC( wld_munmap, 11 /* SYS_munmap */ );

void *wld_mremap( void *old_addr, size_t old_len, size_t new_size, int flags, void *new_addr );
SYSCALL_FUNC( wld_mremap, 25 /* SYS_mremap */ );

int wld_prctl( int code, long arg );
SYSCALL_FUNC( wld_prctl, 157 /* SYS_prctl */ );

pid_t wld_getpid(void);
SYSCALL_NOERR( wld_getpid, 39 /* SYS_getpid */ );

uid_t wld_getuid(void);
SYSCALL_NOERR( wld_getuid, 102 /* SYS_getuid */ );

gid_t wld_getgid(void);
SYSCALL_NOERR( wld_getgid, 104 /* SYS_getgid */ );

uid_t wld_geteuid(void);
SYSCALL_NOERR( wld_geteuid, 107 /* SYS_geteuid */ );

gid_t wld_getegid(void);
SYSCALL_NOERR( wld_getegid, 108 /* SYS_getegid */ );

#elif defined(__aarch64__)

void *thread_data[256];

/*
 * The _start function is the entry and exit point of this program
 *
 *  It calls wld_start, passing a pointer to the args it receives
 *  then jumps to the address wld_start returns.
 */
void _start(void);
extern char __executable_start[];
extern char _end[];
__ASM_GLOBAL_FUNC(_start,
                  "mov x0, SP\n\t"
                  "sub SP, SP, #144\n\t" /* allocate some space for extra aux values */
                  "str x0, [SP]\n\t"     /* orig stack pointer */
                  "ldr x0, =thread_data\n\t"
                  "msr tpidr_el0, x0\n\t"
                  "mov x0, SP\n\t"       /* ptr to orig stack pointer */
                  "bl wld_start\n\t"
                  "ldr x1, [SP]\n\t"     /* new stack pointer */
                  "mov SP, x1\n\t"
                  "mov x30, x0\n\t"
                  "mov x0, #0\n\t"
                  "mov x1, #0\n\t"
                  "mov x2, #0\n\t"
                  "mov x3, #0\n\t"
                  "mov x4, #0\n\t"
                  "mov x5, #0\n\t"
                  "mov x6, #0\n\t"
                  "mov x7, #0\n\t"
                  "mov x8, #0\n\t"
                  "mov x9, #0\n\t"
                  "mov x10, #0\n\t"
                  "mov x11, #0\n\t"
                  "mov x12, #0\n\t"
                  "mov x13, #0\n\t"
                  "mov x14, #0\n\t"
                  "mov x15, #0\n\t"
                  "mov x16, #0\n\t"
                  "mov x17, #0\n\t"
                  "mov x18, #0\n\t"
                  "ret")

#define SYSCALL_FUNC( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "stp x8, x9, [SP, #-16]!\n\t" \
                       "mov x8, #" #nr "\n\t" \
                       "svc #0\n\t" \
                       "ldp x8, x9, [SP], #16\n\t" \
                       "cmn x0, #1, lsl#12\n\t" \
                       "cinv x0, x0, hi\n\t" \
                       "b.hi 1f\n\t" \
                       "ret\n\t" \
                       "1: mov x0, #-1\n\t" \
                       "ret" )

#define SYSCALL_NOERR( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "stp x8, x9, [SP, #-16]!\n\t" \
                       "mov x8, #" #nr "\n\t" \
                       "svc #0\n\t" \
                       "ldp x8, x9, [SP], #16\n\t" \
                       "ret" )

void wld_exit( int code ) __attribute__((noreturn));
SYSCALL_NOERR( wld_exit, 93 /* SYS_exit */ );

ssize_t wld_read( int fd, void *buffer, size_t len );
SYSCALL_FUNC( wld_read, 63 /* SYS_read */ );

ssize_t wld_write( int fd, const void *buffer, size_t len );
SYSCALL_FUNC( wld_write, 64 /* SYS_write */ );

int wld_openat( int dirfd, const char *name, int flags );
SYSCALL_FUNC( wld_openat, 56 /* SYS_openat */ );

int wld_open( const char *name, int flags )
{
    return wld_openat(-100 /* AT_FDCWD */, name, flags);
}

int wld_close( int fd );
SYSCALL_FUNC( wld_close, 57 /* SYS_close */ );

void *wld_mmap( void *start, size_t len, int prot, int flags, int fd, off_t offset );
SYSCALL_FUNC( wld_mmap, 222 /* SYS_mmap */ );

int wld_mprotect( const void *addr, size_t len, int prot );
SYSCALL_FUNC( wld_mprotect, 226 /* SYS_mprotect */ );

int wld_munmap( void *addr, size_t len );
SYSCALL_FUNC( wld_munmap, 215 /* SYS_munmap */ );

void *wld_mremap( void *old_addr, size_t old_len, size_t new_size, int flags, void *new_addr );
SYSCALL_FUNC( wld_mremap, 216 /* SYS_mremap */ );

int wld_prctl( int code, long arg );
SYSCALL_FUNC( wld_prctl, 167 /* SYS_prctl */ );

int wld_rt_sigaction( int signum, const struct wld_sigaction *act, struct wld_sigaction *old_act, size_t sigsetsize );
SYSCALL_FUNC( wld_rt_sigaction, 134 /* SYS_rt_sigaction */ );

static inline int wld_sigaction( int signum, const struct wld_sigaction *act, struct wld_sigaction *old_act )
{
    return wld_rt_sigaction( signum, act, old_act, sizeof(act->wld_sa_mask) );
}

int wld_kill( pid_t pid, int sig );
SYSCALL_FUNC( wld_kill, 129 /* SYS_kill */ );

pid_t wld_getpid(void);
SYSCALL_NOERR( wld_getpid, 172 /* SYS_getpid */ );

uid_t wld_getuid(void);
SYSCALL_NOERR( wld_getuid, 174 /* SYS_getuid */ );

gid_t wld_getgid(void);
SYSCALL_NOERR( wld_getgid, 176 /* SYS_getgid */ );

uid_t wld_geteuid(void);
SYSCALL_NOERR( wld_geteuid, 175 /* SYS_geteuid */ );

gid_t wld_getegid(void);
SYSCALL_NOERR( wld_getegid, 177 /* SYS_getegid */ );

#elif defined(__arm__)

void *thread_data[256];

/*
 * The _start function is the entry and exit point of this program
 *
 *  It calls wld_start, passing a pointer to the args it receives
 *  then jumps to the address wld_start returns.
 */
void _start(void);
extern char __executable_start[];
extern char _end[];
__ASM_GLOBAL_FUNC(_start,
                  "mov r0, sp\n\t"
                  "sub sp, sp, #144\n\t" /* allocate some space for extra aux values */
                  "str r0, [sp]\n\t"     /* orig stack pointer */
                  "ldr r0, =thread_data\n\t"
                  "movw r7, #0x0005\n\t" /* __ARM_NR_set_tls */
                  "movt r7, #0xf\n\t"    /* __ARM_NR_set_tls */
                  "svc #0\n\t"
                  "mov r0, sp\n\t"       /* ptr to orig stack pointer */
                  "bl wld_start\n\t"
                  "ldr r1, [sp]\n\t"     /* new stack pointer */
                  "mov sp, r1\n\t"
                  "mov lr, r0\n\t"
                  "mov r0, #0\n\t"
                  "mov r1, #0\n\t"
                  "mov r2, #0\n\t"
                  "mov r3, #0\n\t"
                  "mov r12, #0\n\t"
                  "bx lr\n\t"
                  ".ltorg\n\t")

#define SYSCALL_FUNC( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "push {r4-r5,r7,lr}\n\t" \
                       "ldr r4, [sp, #16]\n\t" \
                       "ldr r5, [sp, #20]\n\t" \
                       "mov r7, #" #nr "\n\t" \
                       "svc #0\n\t" \
                       "cmn r0, #4096\n\t" \
                       "it hi\n\t" \
                       "movhi r0, #-1\n\t" \
                       "pop {r4-r5,r7,pc}\n\t" )

#define SYSCALL_NOERR( name, nr ) \
    __ASM_GLOBAL_FUNC( name, \
                       "push {r7,lr}\n\t" \
                       "mov r7, #" #nr "\n\t" \
                       "svc #0\n\t" \
                       "pop {r7,pc}\n\t" )

void wld_exit( int code ) __attribute__((noreturn));
SYSCALL_NOERR( wld_exit, 1 /* SYS_exit */ );

ssize_t wld_read( int fd, void *buffer, size_t len );
SYSCALL_FUNC( wld_read, 3 /* SYS_read */ );

ssize_t wld_write( int fd, const void *buffer, size_t len );
SYSCALL_FUNC( wld_write, 4 /* SYS_write */ );

int wld_openat( int dirfd, const char *name, int flags );
SYSCALL_FUNC( wld_openat, 322 /* SYS_openat */ );

int wld_open( const char *name, int flags )
{
    return wld_openat(-100 /* AT_FDCWD */, name, flags);
}

int wld_close( int fd );
SYSCALL_FUNC( wld_close, 6 /* SYS_close */ );

void *wld_mmap2( void *start, size_t len, int prot, int flags, int fd, int offset );
SYSCALL_FUNC( wld_mmap2, 192 /* SYS_mmap2 */ );

void *wld_mmap( void *start, size_t len, int prot, int flags, int fd, off_t offset )
{
    return wld_mmap2(start, len, prot, flags, fd, offset >> 12);
}

int wld_mprotect( const void *addr, size_t len, int prot );
SYSCALL_FUNC( wld_mprotect, 125 /* SYS_mprotect */ );

int wld_munmap( void *addr, size_t len );
SYSCALL_FUNC( wld_munmap, 91 /* SYS_munmap */ );

void *wld_mremap( void *old_addr, size_t old_len, size_t new_size, int flags, void *new_addr );
SYSCALL_FUNC( wld_mremap, 163 /* SYS_mremap */ );

int wld_prctl( int code, long arg );
SYSCALL_FUNC( wld_prctl, 172 /* SYS_prctl */ );

int wld_rt_sigaction( int signum, const struct wld_sigaction *act, struct wld_sigaction *old_act, size_t sigsetsize );
SYSCALL_FUNC( wld_rt_sigaction, 174 /* SYS_rt_sigaction */ );

static inline int wld_sigaction( int signum, const struct wld_sigaction *act, struct wld_sigaction *old_act )
{
    return wld_rt_sigaction( signum, act, old_act, sizeof(act->wld_sa_mask) );
}

int wld_kill( pid_t pid, int sig );
SYSCALL_FUNC( wld_kill, 37 /* SYS_kill */ );

pid_t wld_getpid(void);
SYSCALL_NOERR( wld_getpid, 20 /* SYS_getpid */ );

uid_t wld_getuid(void);
SYSCALL_NOERR( wld_getuid, 24 /* SYS_getuid */ );

gid_t wld_getgid(void);
SYSCALL_NOERR( wld_getgid, 47 /* SYS_getgid */ );

uid_t wld_geteuid(void);
SYSCALL_NOERR( wld_geteuid, 49 /* SYS_geteuid */ );

gid_t wld_getegid(void);
SYSCALL_NOERR( wld_getegid, 50 /* SYS_getegid */ );

unsigned long long __aeabi_uidivmod(unsigned int num, unsigned int den)
{
    unsigned int bit = 1;
    unsigned int quota = 0;
    if (!den)
        wld_exit(1);
    while (den < num && !(den & 0x80000000)) {
        den <<= 1;
        bit <<= 1;
    }
    do {
        if (den <= num) {
            quota |= bit;
            num   -= den;
        }
        bit >>= 1;
        den >>= 1;
    } while (bit);
    return ((unsigned long long)num << 32) | quota;
}

#else
#error preloader not implemented for this CPU
#endif

/* replacement for libc functions */

static int wld_strcmp( const char *str1, const char *str2 )
{
    while (*str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
}

static int wld_strncmp( const char *str1, const char *str2, size_t len )
{
    if (len <= 0) return 0;
    while ((--len > 0) && *str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
}

static inline void *wld_memset( void *dest, int val, size_t len )
{
    char *dst = dest;
    while (len--) *dst++ = val;
    return dest;
}

static size_t wld_strlen( const char *str )
{
    const char *ptr = str;
    while (*ptr) ptr++;
    return ptr - str;
}

static inline void *wld_memmove( void *dest, const void *src, size_t len )
{
    unsigned char *destp = dest;
    const unsigned char *srcp = src;

    /* Two area overlaps and src precedes dest?
     *
     * Note: comparing pointers to different objects leads to undefined
     * behavior in C; therefore, we cast them to unsigned long for comparison
     * (which is implementation-defined instead).  This also allows us to rely
     * on unsigned overflow on dest < src (forward copy case) in which case the
     * LHS exceeds len and makes the condition false.
     */
    if ((unsigned long)dest - (unsigned long)src < len)
    {
        destp += len;
        srcp += len;
        while (len--) *--destp = *--srcp;
    }
    else
    {
        while (len--) *destp++ = *srcp++;
    }

    return dest;
}

static inline void *wld_memchr( const void *mem, int val, size_t len )
{
    const unsigned char *ptr = mem, *end = (const unsigned char *)ptr + len;

    for (ptr = mem; ptr != end; ptr++)
        if (*ptr == (unsigned char)val)
            return (void *)ptr;

    return NULL;
}

/*
 * parse_ul - parse an unsigned long number with given radix
 *
 * Differences from strtoul():
 * - Does not support radix prefixes ("0x", etc)
 * - Does not saturate to ULONG_MAX on overflow, wrap around instead
 * - Indicates overflow via output argument, not errno
 */
static inline unsigned long parse_ul( const char *nptr, char **endptr, unsigned int radix, int *overflow )
{
    const char *p = nptr;
    unsigned long value, max_radix_mul;
    int ovfl = 0;

    value = 0;
    max_radix_mul = ULONG_MAX / radix;
    for (;;)
    {
        unsigned int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;
        if (digit >= radix) break;
        if (value > max_radix_mul) ovfl = 1;
        value *= radix;
        if (value > value + digit) ovfl = 1;
        value += digit;
        p++;
    }

    if (endptr) *endptr = (char *)p;
    if (overflow) *overflow = ovfl;
    return value;
}

/*
 * wld_printf - just the basics
 *
 *  %x prints a hex number
 *  %s prints a string
 *  %p prints a pointer
 */
static int wld_vsprintf(char *buffer, const char *fmt, va_list args )
{
    static const char hex_chars[16] = "0123456789abcdef";
    const char *p = fmt;
    char *str = buffer;
    int i;

    while( *p )
    {
        if( *p == '%' )
        {
            p++;
            if( *p == 'x' )
            {
                unsigned int x = va_arg( args, unsigned int );
                for (i = 2*sizeof(x) - 1; i >= 0; i--)
                    *str++ = hex_chars[(x>>(i*4))&0xf];
            }
            else if (p[0] == 'l' && p[1] == 'x')
            {
                unsigned long x = va_arg( args, unsigned long );
                for (i = 2*sizeof(x) - 1; i >= 0; i--)
                    *str++ = hex_chars[(x>>(i*4))&0xf];
                p++;
            }
            else if( *p == 'p' )
            {
                unsigned long x = (unsigned long)va_arg( args, void * );
                for (i = 2*sizeof(x) - 1; i >= 0; i--)
                    *str++ = hex_chars[(x>>(i*4))&0xf];
            }
            else if( *p == 's' )
            {
                char *s = va_arg( args, char * );
                while(*s)
                    *str++ = *s++;
            }
            else if( *p == 0 )
                break;
            p++;
        }
        *str++ = *p++;
    }
    *str = 0;
    return str - buffer;
}

static __attribute__((format(printf,1,2))) void wld_printf(const char *fmt, ... )
{
    va_list args;
    char buffer[256];
    int len;

    va_start( args, fmt );
    len = wld_vsprintf(buffer, fmt, args );
    va_end( args );
    wld_write(2, buffer, len);
}

static __attribute__((noreturn,format(printf,1,2))) void fatal_error(const char *fmt, ... )
{
    va_list args;
    char buffer[256];
    int len;

    va_start( args, fmt );
    len = wld_vsprintf(buffer, fmt, args );
    va_end( args );
    wld_write(2, buffer, len);
    wld_exit(1);
}

#ifdef DUMP_AUX_INFO
/*
 *  Dump interesting bits of the ELF auxv_t structure that is passed
 *   as the 4th parameter to the _start function
 */
static void dump_auxiliary( struct wld_auxv *av )
{
#define NAME(at) { at, #at }
    static const struct { int val; const char *name; } names[] =
    {
        NAME(AT_BASE),
        NAME(AT_CLKTCK),
        NAME(AT_EGID),
        NAME(AT_ENTRY),
        NAME(AT_EUID),
        NAME(AT_FLAGS),
        NAME(AT_GID),
        NAME(AT_HWCAP),
        NAME(AT_PAGESZ),
        NAME(AT_PHDR),
        NAME(AT_PHENT),
        NAME(AT_PHNUM),
        NAME(AT_PLATFORM),
        NAME(AT_SYSINFO),
        NAME(AT_SYSINFO_EHDR),
        NAME(AT_UID),
        { 0, NULL }
    };
#undef NAME

    int i;

    for (  ; av->a_type != AT_NULL; av++)
    {
        for (i = 0; names[i].name; i++) if (names[i].val == av->a_type) break;
        if (names[i].name) wld_printf("%s = %lx\n", names[i].name, (unsigned long)av->a_un.a_val);
        else wld_printf( "%lx = %lx\n", (unsigned long)av->a_type, (unsigned long)av->a_un.a_val );
    }
}
#endif

/*
 * parse_stackargs
 *
 * parse out the initial stack for argv, envp, and etc., and store the
 * information into the given stackarg_info structure.
 */
static void parse_stackargs( struct stackarg_info *outinfo, void *stack )
{
    int argc;
    char **argv, **envp, **env_end;
    struct wld_auxv *auxv, *auxv_end;

    argc = *(int *)stack;
    argv = (char **)stack + 1;
    envp = argv + (unsigned int)argc + 1;

    env_end = envp;
    while (*env_end++)
        ;
    auxv = (struct wld_auxv *)env_end;

    auxv_end = auxv;
    while ((auxv_end++)->a_type != AT_NULL)
        ;

    outinfo->stack = stack;
    outinfo->argc = argc;
    outinfo->argv = argv;
    outinfo->envp = envp;
    outinfo->auxv = auxv;
    outinfo->auxv_end = auxv_end;
}

/*
 * stackargs_getenv
 *
 * Retrieve the value of an environment variable from stackarg_info.
 */
static char *stackargs_getenv( const struct stackarg_info *info, const char *name )
{
    size_t namelen = wld_strlen( name );
    char **envp;

    for (envp = info->envp; *envp; envp++)
    {
        if (wld_strncmp( *envp, name, namelen ) == 0 &&
            (*envp)[namelen] == '=') return *envp + namelen + 1;
    }

    return NULL;
}

/*
 * stackargs_shift_args
 *
 * Remove the specific number of arguments from the start of argv.
 */
static void stackargs_shift_args( struct stackarg_info *info, int num_args )
{
    info->stack = (char **)info->stack + num_args;
    info->argc -= num_args;
    info->argv = (char **)info->stack + 1;

    wld_memset( info->stack, 0, sizeof(char *) );
    /* Don't coalesce zeroing and setting argc -- we *might* support big endian in the future */
    *(int *)info->stack = info->argc;
}

/*
 * set_auxiliary_values
 *
 * Set the new auxiliary values
 */
static void set_auxiliary_values( struct wld_auxv *av, const struct wld_auxv *new_av,
                                  const struct wld_auxv *delete_av, void **stack )
{
    int i, j, av_count = 0, new_count = 0, delete_count = 0;
    char *src, *dst;

    /* count how many aux values we have already */
    while (av[av_count].a_type != AT_NULL) av_count++;

    /* delete unwanted values */
    for (j = 0; delete_av[j].a_type != AT_NULL; j++)
    {
        for (i = 0; i < av_count; i++) if (av[i].a_type == delete_av[j].a_type)
        {
            av[i].a_type = av[av_count-1].a_type;
            av[i].a_un.a_val = av[av_count-1].a_un.a_val;
            av[--av_count].a_type = AT_NULL;
            delete_count++;
            break;
        }
    }

    /* count how many values we have in new_av that aren't in av */
    for (j = 0; new_av[j].a_type != AT_NULL; j++)
    {
        for (i = 0; i < av_count; i++) if (av[i].a_type == new_av[j].a_type) break;
        if (i == av_count) new_count++;
    }

    src = (char *)*stack;
    dst = src - (new_count - delete_count) * sizeof(*av);
    dst = (char *)((unsigned long)dst & ~15);
    if (dst < src)   /* need to make room for the extra values */
    {
        int len = (char *)(av + av_count + 1) - src;
        for (i = 0; i < len; i++) dst[i] = src[i];
    }
    else if (dst > src)  /* get rid of unused values */
    {
        int len = (char *)(av + av_count + 1) - src;
        for (i = len - 1; i >= 0; i--) dst[i] = src[i];
    }
    *stack = dst;
    av = (struct wld_auxv *)((char *)av + (dst - src));

    /* now set the values */
    for (j = 0; new_av[j].a_type != AT_NULL; j++)
    {
        for (i = 0; i < av_count; i++) if (av[i].a_type == new_av[j].a_type) break;
        if (i < av_count) av[i].a_un.a_val = new_av[j].a_un.a_val;
        else
        {
            av[av_count].a_type     = new_av[j].a_type;
            av[av_count].a_un.a_val = new_av[j].a_un.a_val;
            av_count++;
        }
    }

#ifdef DUMP_AUX_INFO
    wld_printf("New auxiliary info:\n");
    dump_auxiliary( av );
#endif
}

/*
 * get_auxiliary
 *
 * Get a field of the auxiliary structure
 */
static ElfW(Addr) get_auxiliary( struct wld_auxv *av, int type, ElfW(Addr) def_val )
{
  for ( ; av->a_type != AT_NULL; av++)
      if( av->a_type == type ) return av->a_un.a_val;
  return def_val;
}

/*
 * map_so_lib
 *
 * modelled after _dl_map_object_from_fd() from glibc-2.3.1/elf/dl-load.c
 *
 * This function maps the segments from an ELF object, and optionally
 *  stores information about the mapping into the auxv_t structure.
 */
static void map_so_lib( const char *name, struct wld_link_map *l)
{
    int fd;
    unsigned char buf[0x800];
    ElfW(Ehdr) *header = (ElfW(Ehdr)*)buf;
    ElfW(Phdr) *phdr, *ph;
    /* Scan the program header table, collecting its load commands.  */
    struct loadcmd
      {
        ElfW(Addr) mapstart, mapend, dataend, allocend;
        off_t mapoff;
        int prot;
      } loadcmds[16], *c;
    size_t nloadcmds = 0, maplength;

    fd = wld_open( name, O_RDONLY );
    if (fd == -1) fatal_error("%s: could not open\n", name );

    if (wld_read( fd, buf, sizeof(buf) ) != sizeof(buf))
        fatal_error("%s: failed to read ELF header\n", name);

    phdr = (void*) (((unsigned char*)buf) + header->e_phoff);

    if( ( header->e_ident[0] != 0x7f ) ||
        ( header->e_ident[1] != 'E' ) ||
        ( header->e_ident[2] != 'L' ) ||
        ( header->e_ident[3] != 'F' ) )
        fatal_error( "%s: not an ELF binary... don't know how to load it\n", name );

#ifdef __i386__
    if( header->e_machine != EM_386 )
        fatal_error("%s: not an i386 ELF binary... don't know how to load it\n", name );
#elif defined(__x86_64__)
    if( header->e_machine != EM_X86_64 )
        fatal_error("%s: not an x86-64 ELF binary... don't know how to load it\n", name );
#elif defined(__aarch64__)
    if( header->e_machine != EM_AARCH64 )
        fatal_error("%s: not an aarch64 ELF binary... don't know how to load it\n", name );
#elif defined(__arm__)
    if( header->e_machine != EM_ARM )
        fatal_error("%s: not an arm ELF binary... don't know how to load it\n", name );
#endif

    if (header->e_phnum > sizeof(loadcmds)/sizeof(loadcmds[0]))
        fatal_error( "%s: oops... not enough space for load commands\n", name );

    maplength = header->e_phnum * sizeof (ElfW(Phdr));
    if (header->e_phoff + maplength > sizeof(buf))
        fatal_error( "%s: oops... not enough space for ELF headers\n", name );

    l->l_ld = 0;
    l->l_addr = 0;
    l->l_phdr = 0;
    l->l_phnum = header->e_phnum;
    l->l_entry = header->e_entry;
    l->l_interp = 0;

    for (ph = phdr; ph < &phdr[l->l_phnum]; ++ph)
    {

#ifdef DUMP_SEGMENTS
      wld_printf( "ph = %p\n", ph );
      wld_printf( " p_type   = %lx\n", (unsigned long)ph->p_type );
      wld_printf( " p_flags  = %lx\n", (unsigned long)ph->p_flags );
      wld_printf( " p_offset = %lx\n", (unsigned long)ph->p_offset );
      wld_printf( " p_vaddr  = %lx\n", (unsigned long)ph->p_vaddr );
      wld_printf( " p_paddr  = %lx\n", (unsigned long)ph->p_paddr );
      wld_printf( " p_filesz = %lx\n", (unsigned long)ph->p_filesz );
      wld_printf( " p_memsz  = %lx\n", (unsigned long)ph->p_memsz );
      wld_printf( " p_align  = %lx\n", (unsigned long)ph->p_align );
#endif

      switch (ph->p_type)
        {
          /* These entries tell us where to find things once the file's
             segments are mapped in.  We record the addresses it says
             verbatim, and later correct for the run-time load address.  */
        case PT_DYNAMIC:
          l->l_ld = (void *) ph->p_vaddr;
          l->l_ldnum = ph->p_memsz / sizeof (Elf32_Dyn);
          break;

        case PT_PHDR:
          l->l_phdr = (void *) ph->p_vaddr;
          break;

        case PT_LOAD:
          {
            if ((ph->p_align & page_mask) != 0)
              fatal_error( "%s: ELF load command alignment not page-aligned\n", name );

            if (((ph->p_vaddr - ph->p_offset) & (ph->p_align - 1)) != 0)
              fatal_error( "%s: ELF load command address/offset not properly aligned\n", name );

            c = &loadcmds[nloadcmds++];
            c->mapstart = ph->p_vaddr & ~(ph->p_align - 1);
            c->mapend = ((ph->p_vaddr + ph->p_filesz + page_mask) & ~page_mask);
            c->dataend = ph->p_vaddr + ph->p_filesz;
            c->allocend = ph->p_vaddr + ph->p_memsz;
            c->mapoff = ph->p_offset & ~(ph->p_align - 1);

            c->prot = 0;
            if (ph->p_flags & PF_R)
              c->prot |= PROT_READ;
            if (ph->p_flags & PF_W)
              c->prot |= PROT_WRITE;
            if (ph->p_flags & PF_X)
              c->prot |= PROT_EXEC;
          }
          break;

        case PT_INTERP:
          l->l_interp = ph->p_vaddr;
          break;

        case PT_TLS:
          /*
           * We don't need to set anything up because we're
           * emulating the kernel, not ld-linux.so.2
           * The ELF loader will set up the TLS data itself.
           */
        case PT_SHLIB:
        case PT_NOTE:
        default:
          break;
        }
    }

    /* Now process the load commands and map segments into memory.  */
    if (!nloadcmds)
        fatal_error( "%s: no segments to load\n", name );
    c = loadcmds;

    /* Length of the sections to be loaded.  */
    maplength = loadcmds[nloadcmds - 1].allocend - c->mapstart;

    if( header->e_type == ET_DYN )
    {
        ElfW(Addr) mappref;
        mappref = (ELF_PREFERRED_ADDRESS (loader, maplength, c->mapstart)
                   - MAP_BASE_ADDR (l));

        /* Remember which part of the address space this object uses.  */
        l->l_map_start = (ElfW(Addr)) wld_mmap ((void *) mappref, maplength,
                                              c->prot, MAP_COPY | MAP_FILE,
                                              fd, c->mapoff);
        /* wld_printf("set  : offset = %x\n", c->mapoff); */
        /* wld_printf("l->l_map_start = %x\n", l->l_map_start); */

        l->l_map_end = l->l_map_start + maplength;
        l->l_addr = l->l_map_start - c->mapstart;

        wld_mprotect ((caddr_t) (l->l_addr + c->mapend),
                    loadcmds[nloadcmds - 1].allocend - c->mapend,
                    PROT_NONE);
        goto postmap;
    }
    else
    {
        /* sanity check */
        if ((char *)c->mapstart + maplength > preloader_start &&
            (char *)c->mapstart <= preloader_end)
            fatal_error( "%s: binary overlaps preloader (%p-%p)\n",
                         name, (char *)c->mapstart, (char *)c->mapstart + maplength );

        ELF_FIXED_ADDRESS (loader, c->mapstart);
    }

    /* Remember which part of the address space this object uses.  */
    l->l_map_start = c->mapstart + l->l_addr;
    l->l_map_end = l->l_map_start + maplength;

    while (c < &loadcmds[nloadcmds])
      {
        if (c->mapend > c->mapstart)
            /* Map the segment contents from the file.  */
            wld_mmap ((void *) (l->l_addr + c->mapstart),
                        c->mapend - c->mapstart, c->prot,
                        MAP_FIXED | MAP_COPY | MAP_FILE, fd, c->mapoff);

      postmap:
        if (l->l_phdr == 0
            && (ElfW(Off)) c->mapoff <= header->e_phoff
            && ((size_t) (c->mapend - c->mapstart + c->mapoff)
                >= header->e_phoff + header->e_phnum * sizeof (ElfW(Phdr))))
          /* Found the program header in this segment.  */
          l->l_phdr = (void *)(unsigned long)(c->mapstart + header->e_phoff - c->mapoff);

        if (c->allocend > c->dataend)
          {
            /* Extra zero pages should appear at the end of this segment,
               after the data mapped from the file.   */
            ElfW(Addr) zero, zeroend, zeropage;

            zero = l->l_addr + c->dataend;
            zeroend = l->l_addr + c->allocend;
            zeropage = (zero + page_mask) & ~page_mask;

            /*
             * This is different from the dl-load load...
             *  ld-linux.so.2 relies on the whole page being zero'ed
             */
            zeroend = (zeroend + page_mask) & ~page_mask;

            if (zeroend < zeropage)
            {
              /* All the extra data is in the last page of the segment.
                 We can just zero it.  */
              zeropage = zeroend;
            }

            if (zeropage > zero)
              {
                /* Zero the final part of the last page of the segment.  */
                if ((c->prot & PROT_WRITE) == 0)
                  {
                    /* Dag nab it.  */
                    wld_mprotect ((caddr_t) (zero & ~page_mask), page_size, c->prot|PROT_WRITE);
                  }
                wld_memset ((void *) zero, '\0', zeropage - zero);
                if ((c->prot & PROT_WRITE) == 0)
                  wld_mprotect ((caddr_t) (zero & ~page_mask), page_size, c->prot);
              }

            if (zeroend > zeropage)
              {
                /* Map the remaining zero pages in from the zero fill FD.  */
                wld_mmap ((caddr_t) zeropage, zeroend - zeropage,
                                c->prot, MAP_ANON|MAP_PRIVATE|MAP_FIXED,
                                -1, 0);
              }
          }

        ++c;
      }

    if (l->l_phdr == NULL) fatal_error("no program header\n");

    l->l_phdr = (void *)((ElfW(Addr))l->l_phdr + l->l_addr);
    l->l_entry += l->l_addr;

    wld_close( fd );
}


static unsigned int wld_elf_hash( const char *name )
{
    unsigned int hi, hash = 0;
    while (*name)
    {
        hash = (hash << 4) + (unsigned char)*name++;
        hi = hash & 0xf0000000;
        hash ^= hi;
        hash ^= hi >> 24;
    }
    return hash;
}

static unsigned int gnu_hash( const char *name )
{
    unsigned int h = 5381;
    while (*name) h = h * 33 + (unsigned char)*name++;
    return h;
}

/*
 * Find a symbol in the symbol table of the executable loaded
 */
static void *find_symbol( const struct wld_link_map *map, const char *var, int type )
{
    const ElfW(Dyn) *dyn = NULL;
    const ElfW(Phdr) *ph;
    const ElfW(Sym) *symtab = NULL;
    const Elf32_Word *hashtab = NULL;
    const Elf32_Word *gnu_hashtab = NULL;
    const char *strings = NULL;
    Elf32_Word idx;

    /* check the values */
#ifdef DUMP_SYMS
    wld_printf("%p %x\n", map->l_phdr, map->l_phnum );
#endif
    /* parse the (already loaded) ELF executable's header */
    for (ph = map->l_phdr; ph < &map->l_phdr[map->l_phnum]; ++ph)
    {
        if( PT_DYNAMIC == ph->p_type )
        {
            dyn = (void *)(ph->p_vaddr + map->l_addr);
            break;
        }
    }
    if( !dyn ) return NULL;

    while( dyn->d_tag )
    {
        if( dyn->d_tag == DT_STRTAB )
            strings = (const char*)(dyn->d_un.d_ptr + map->l_addr);
        if( dyn->d_tag == DT_SYMTAB )
            symtab = (const ElfW(Sym) *)(dyn->d_un.d_ptr + map->l_addr);
        if( dyn->d_tag == DT_HASH )
            hashtab = (const Elf32_Word *)(dyn->d_un.d_ptr + map->l_addr);
        if( dyn->d_tag == DT_GNU_HASH )
            gnu_hashtab = (const Elf32_Word *)(dyn->d_un.d_ptr + map->l_addr);
#ifdef DUMP_SYMS
        wld_printf("%lx %p\n", (unsigned long)dyn->d_tag, (void *)dyn->d_un.d_ptr );
#endif
        dyn++;
    }

    if( (!symtab) || (!strings) ) return NULL;

    if (gnu_hashtab)  /* new style hash table */
    {
        const unsigned int hash   = gnu_hash(var);
        const Elf32_Word nbuckets = gnu_hashtab[0];
        const Elf32_Word symbias  = gnu_hashtab[1];
        const Elf32_Word nwords   = gnu_hashtab[2];
        const ElfW(Addr) *bitmask = (const ElfW(Addr) *)(gnu_hashtab + 4);
        const Elf32_Word *buckets = (const Elf32_Word *)(bitmask + nwords);
        const Elf32_Word *chains  = buckets + nbuckets - symbias;

        if (!(idx = buckets[hash % nbuckets])) return NULL;
        do
        {
            if ((chains[idx] & ~1u) == (hash & ~1u) &&
                ELF32_ST_BIND(symtab[idx].st_info) == STB_GLOBAL &&
                ELF32_ST_TYPE(symtab[idx].st_info) == type &&
                !wld_strcmp( strings + symtab[idx].st_name, var ))
                goto found;
        } while (!(chains[idx++] & 1u));
    }
    else if (hashtab)  /* old style hash table */
    {
        const unsigned int hash   = wld_elf_hash(var);
        const Elf32_Word nbuckets = hashtab[0];
        const Elf32_Word *buckets = hashtab + 2;
        const Elf32_Word *chains  = buckets + nbuckets;

        for (idx = buckets[hash % nbuckets]; idx; idx = chains[idx])
        {
            if (ELF32_ST_BIND(symtab[idx].st_info) == STB_GLOBAL &&
                ELF32_ST_TYPE(symtab[idx].st_info) == type &&
                !wld_strcmp( strings + symtab[idx].st_name, var ))
                goto found;
        }
    }
    return NULL;

found:
#ifdef DUMP_SYMS
    wld_printf("Found %s -> %p\n", strings + symtab[idx].st_name, (void *)symtab[idx].st_value );
#endif
    return (void *)(symtab[idx].st_value + map->l_addr);
}

/*
 *  preload_reserve
 *
 * Reserve a range specified in string format
 */
static void preload_reserve( const char *str )
{
    char *p = (char *)str;
    unsigned long result = 0;
    void *start = NULL, *end = NULL;
    int i;

    result = parse_ul( p, &p, 16, NULL );
    if (*p == '-')
    {
        start = (void *)(result & ~page_mask);
        result = parse_ul( p + 1, &p, 16, NULL );
        if (*p) goto error;
        end = (void *)((result + page_mask) & ~page_mask);
    }
    else if (*p || result) goto error;  /* single value '0' is allowed */

    /* sanity checks */
    if (end <= start) start = end = NULL;
    else if ((char *)end > preloader_start &&
             (char *)start <= preloader_end)
    {
        wld_printf( "WINEPRELOADRESERVE range %p-%p overlaps preloader %p-%p\n",
                     start, end, preloader_start, preloader_end );
        start = end = NULL;
    }

    /* check for overlap with low memory areas */
    for (i = 0; preload_info[i].size; i++)
    {
        if ((char *)preload_info[i].addr > (char *)0x00110000) break;
        if ((char *)end <= (char *)preload_info[i].addr + preload_info[i].size)
        {
            start = end = NULL;
            break;
        }
        if ((char *)start < (char *)preload_info[i].addr + preload_info[i].size)
            start = (char *)preload_info[i].addr + preload_info[i].size;
    }

    while (preload_info[i].size) i++;
    preload_info[i].addr = start;
    preload_info[i].size = (char *)end - (char *)start;
    return;

error:
    fatal_error( "invalid WINEPRELOADRESERVE value '%s'\n", str );
}

/*
 * find_preload_reserved_area
 *
 * Check if the given address range overlaps with one of the reserved ranges.
 */
static int find_preload_reserved_area( const void *addr, size_t size )
{
    /* Make the interval inclusive to avoid integer overflow. */
    unsigned long start = (unsigned long)addr;
    unsigned long end = (unsigned long)addr + size - 1;
    int i;

    /* Handle size == 0 specifically since "end" may overflow otherwise. */
    if (!size)
        return -1;

    for (i = 0; preload_info[i].size; i++)
    {
        if (end   >= (unsigned long)preload_info[i].addr &&
            start <  (unsigned long)preload_info[i].addr + preload_info[i].size)
            return i;
    }
    return -1;
}

/* remove a range from the preload list */
static void remove_preload_range( int i )
{
    while (preload_info[i].size)
    {
        preload_info[i].addr = preload_info[i+1].addr;
        preload_info[i].size = preload_info[i+1].size;
        i++;
    }
}

/*
 *  is_in_preload_range
 *
 * Check if address of the given aux value is in one of the reserved ranges
 */
static int is_in_preload_range( const struct wld_auxv *av, int type )
{
    while (av->a_type != AT_NULL)
    {
        if (av->a_type == type) return find_preload_reserved_area( (const void *)av->a_un.a_val, 1 ) >= 0;
        av++;
    }
    return 0;
}

/* set the process name if supported */
static void set_process_name( int argc, char *argv[] )
{
    int i;
    unsigned int off;
    char *p, *name, *end;

    /* set the process short name */
    for (p = name = argv[1]; *p; p++) if (p[0] == '/' && p[1]) name = p + 1;
    if (wld_prctl( 15 /* PR_SET_NAME */, (long)name ) == -1) return;

    /* find the end of the argv array and move everything down */
    end = argv[argc - 1];
    while (*end) end++;
    off = argv[1] - argv[0];
    for (p = argv[1]; p <= end; p++) *(p - off) = *p;
    wld_memset( end - off, 0, off );
    for (i = 1; i < argc; i++) argv[i] -= off;
}

/*
 * linebuffer_init
 *
 * Initialise a linebuffer with the given buffer.
 */
static void linebuffer_init( struct linebuffer *lbuf, char *base, size_t len )
{
    lbuf->base = base;
    lbuf->limit = base + (len - 1);  /* NULL terminator */
    lbuf->head = base;
    lbuf->tail = base;
    lbuf->truncated = 0;
}

/*
 * linebuffer_getline
 *
 * Retrieve a line from the linebuffer.
 * If a line is longer than the allocated buffer, then the line is truncated;
 * the truncated flag is set to indicate this condition.
 */
static char *linebuffer_getline( struct linebuffer *lbuf )
{
    char *lnp, *line;

    while ((lnp = wld_memchr( lbuf->tail, '\n', lbuf->head - lbuf->tail )))
    {
        /* Consume the current line from the buffer. */
        line = lbuf->tail;
        lbuf->tail = lnp + 1;

        if (!lbuf->truncated)
        {
            *lnp = '\0';
            return line;
        }

        /* Remainder of a previously truncated line; ignore it. */
        lbuf->truncated = 0;
    }

    if (lbuf->tail == lbuf->base && lbuf->head == lbuf->limit)
    {
        /* We have not encountered the end of the current line yet; however,
         * the buffer is full and cannot be compacted to accept more
         * characters.  Truncate the line here, and consume it from the buffer.
         */
        line = lbuf->tail;
        lbuf->tail = lbuf->head;

        /* Ignore any further characters until the start of the next line. */
        lbuf->truncated = 1;
        *lbuf->head = '\0';
        return line;
    }

    if (lbuf->tail != lbuf->base)
    {
        /* Compact the buffer.  Make room for reading more data by zapping the
         * leading gap in the buffer.
         */
        wld_memmove( lbuf->base, lbuf->tail, lbuf->head - lbuf->tail);
        lbuf->head -= lbuf->tail - lbuf->base;
        lbuf->tail = lbuf->base;
    }

    return NULL;
}

/*
 * parse_maps_line
 *
 * Parse an entry from /proc/self/maps file into a vma_area structure.
 */
static int parse_maps_line( struct vma_area *entry, const char *line )
{
    struct vma_area item = { 0 };
    unsigned long dev_maj, dev_min;
    char *ptr = (char *)line;
    int overflow;

    item.start = parse_ul( ptr, &ptr, 16, &overflow );
    if (overflow) return -1;
    if (*ptr != '-') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    item.end = parse_ul( ptr, &ptr, 16, &overflow );
    if (overflow) item.end = -page_size;
    if (*ptr != ' ') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    if (item.start >= item.end) return -1;

    if (*ptr != 'r' && *ptr != '-') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;
    if (*ptr != 'w' && *ptr != '-') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;
    if (*ptr != 'x' && *ptr != '-') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;
    if (*ptr != 's' && *ptr != 'p') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;
    if (*ptr != ' ') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    parse_ul( ptr, &ptr, 16, NULL );
    if (*ptr != ' ') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    dev_maj = parse_ul( ptr, &ptr, 16, NULL );
    if (*ptr != ':') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    dev_min = parse_ul( ptr, &ptr, 16, NULL );
    if (*ptr != ' ') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    parse_ul( ptr, &ptr, 10, NULL );
    if (*ptr != ' ') fatal_error( "parse error in /proc/self/maps\n" );
    ptr++;

    while (*ptr == ' ')
        ptr++;

    if (dev_maj == 0 && dev_min == 0)
    {
        if (wld_strcmp(ptr, "[vdso]") == 0)
            item.type_flags |= VMA_VDSO;
        else if (wld_strcmp(ptr, "[vvar]") == 0)
            item.type_flags |= VMA_VVAR;
    }

    *entry = item;
    return 0;
}

/*
 * lookup_vma_entry
 *
 * Find the first VMA of which end address is greater than the given address.
 */
static struct vma_area *lookup_vma_entry( const struct vma_area_list *list, unsigned long address )
{
    const struct vma_area *left = list->base, *right = list->list_end, *mid;
    while (left < right)
    {
        mid = left + (right - left) / 2;
        if (mid->end <= address) left = mid + 1;
        else right = mid;
    }
    return (struct vma_area *)left;
}

/*
 * map_reserve_range
 *
 * Reserve the specified address range.
 * If there are any existing VMAs in the range, they are replaced.
 */
static int map_reserve_range( void *addr, size_t size )
{
    if (addr == (void *)-1 ||
        wld_mmap( addr, size, PROT_NONE,
                  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0) != addr)
        return -1;
    return 0;
}

/*
 * map_reserve_unmapped_range
 *
 * Reserve the specified address range excluding already mapped areas.
 */
static int map_reserve_unmapped_range( const struct vma_area_list *list, void *addr, size_t size )
{
    unsigned long range_start = (unsigned long)addr,
                  range_end = (unsigned long)addr + size,
                  last_addr;
    const struct vma_area *start, *item;

    last_addr = range_start;
    start = lookup_vma_entry( list, range_start );
    for (item = start; item != list->list_end && item->start < range_end; item++)
    {
        if (item->start > last_addr &&
            map_reserve_range( (void *)last_addr, item->start - last_addr ) < 0)
            goto fail;
        last_addr = item->end;
    }

    if (range_end > last_addr &&
        map_reserve_range( (void *)last_addr, range_end - last_addr ) < 0)
        goto fail;
    return 0;

fail:
    while (item != start)
    {
        item--;
        last_addr = item == start ? range_start : item[-1].end;
        if (item->start > last_addr)
            wld_munmap( (void *)last_addr, item->start - last_addr );
    }
    return -1;
}

/*
 * insert_vma_entry
 *
 * Insert the given VMA into the list.
 */
static void insert_vma_entry( struct vma_area_list *list, const struct vma_area *item )
{
    struct vma_area *left = list->base, *right = list->list_end, *mid;

    if (left < right)
    {
        mid = right - 1;  /* optimisation: start search from end */
        for (;;)
        {
            if (mid->end < item->end) left = mid + 1;
            else right = mid;
            if (left >= right) break;
            mid = left + (right - left) / 2;
        }
    }
    wld_memmove(left + 1, left, list->list_end - left);
    wld_memmove(left, item, sizeof(*item));
    list->list_end++;
    return;
}

/*
 * find_vma_envelope_range
 *
 * Compute the smallest range that contains all VMAs with any of the given
 * type flags.
 */
static int find_vma_envelope_range( const struct vma_area_list *list, int type_mask, unsigned long *startp, unsigned long *sizep )
{
    const struct vma_area *item;
    unsigned long start = ULONG_MAX;
    unsigned long end = 0;

    FOREACH_VMA(list, item)
    {
        if (item->type_flags & type_mask)
        {
            if (start > item->start) start = item->start;
            if (end < item->end) end = item->end;
        }
    }

    if (start >= end) return -1;

    *startp = start;
    *sizep = end - start;
    return 0;
}

/*
 * remap_multiple_vmas
 *
 * Relocate all VMAs with the given type flags.
 * This function can also be used to reverse the effects of previous
 * remap_multiple_vmas().
 */
static int remap_multiple_vmas( struct vma_area_list *list, unsigned long delta, int type_mask, unsigned char revert )
{
    struct vma_area *item;
    void *old_addr, *desired_addr, *mapped_addr;
    size_t size;

    FOREACH_VMA(list, item)
    {
        if ((item->type_flags & type_mask) && item->moved == revert)
        {
            if (revert)
            {
                old_addr = (void *)(item->start + delta);
                desired_addr = (void *)item->start;
            }
            else
            {
                old_addr = (void *)item->start;
                desired_addr = (void *)(item->start + delta);
            }
            size = item->end - item->start;
            mapped_addr = wld_mremap( old_addr, size, size, MREMAP_FIXED | MREMAP_MAYMOVE, desired_addr );
            if (mapped_addr == (void *)-1) return -1;
            if (mapped_addr != desired_addr)
            {
                if (mapped_addr == old_addr) return -1;  /* kernel deoesn't support MREMAP_FIXED */
                fatal_error( "mremap() returned different address\n" );
            }
            item->moved = !revert;
        }
    }

    return 0;
}

/*
 * scan_vma
 *
 * Parse /proc/self/maps into the given VMA area list.
 */
static void scan_vma( struct vma_area_list *list, size_t *real_count )
{
    int fd;
    size_t n = 0;
    ssize_t nread;
    struct linebuffer lbuf;
    char buffer[80 + PATH_MAX], *line;
    struct vma_area item;

    fd = wld_open( "/proc/self/maps", O_RDONLY );
    if (fd == -1) fatal_error( "could not open /proc/self/maps\n" );

    linebuffer_init(&lbuf, buffer, sizeof(buffer));
    for (;;)
    {
        nread = wld_read( fd, lbuf.head, lbuf.limit - lbuf.head );
        if (nread < 0) fatal_error( "could not read /proc/self/maps\n" );
        if (nread == 0) break;
        lbuf.head += nread;

        while ((line = linebuffer_getline( &lbuf )))
        {
            if (parse_maps_line( &item, line ) >= 0)
            {
                if (list->list_end < list->alloc_end) insert_vma_entry( list, &item );
                n++;
            }
        }
    }

    wld_close(fd);
    *real_count = n;
}

/*
 * unmap_range_keep_reservations
 *
 * Equivalent to munmap(), except that any area overlapping with preload ranges
 * are not unmapped but instead (re-)reserved with map_reserve_range().
 */
static void unmap_range_keep_reservations( void *addr, size_t size )
{
    unsigned long range_start = (unsigned long)addr,
                  range_end = (unsigned long)addr + size,
                  seg_start, reserve_start, reserve_end;
    int i;

    for (seg_start = range_start; seg_start < range_end; seg_start = reserve_end)
    {
        reserve_start = range_end;
        reserve_end = range_end;

        for (i = 0; preload_info[i].size; i++)
        {
            if ((unsigned long)preload_info[i].addr + preload_info[i].size > seg_start &&
                (unsigned long)preload_info[i].addr < reserve_start)
            {
                reserve_start = (unsigned long)preload_info[i].addr;
                reserve_end = reserve_start + preload_info[i].size;
            }
        }

        if (reserve_start < seg_start) reserve_start = seg_start;
        if (reserve_end > range_end) reserve_end = range_end;

        if (reserve_start > seg_start &&
            wld_munmap( (void *)seg_start, reserve_start - seg_start) < 0)
            wld_printf( "preloader: Warning: failed to unmap range %p-%p\n",
                        (void *)seg_start, (void *)reserve_start );

        if (reserve_start < reserve_end &&
            map_reserve_range( (void *)reserve_start, reserve_end - reserve_start ) < 0)
            wld_printf( "preloader: Warning: failed to free and reserve range %p-%p\n",
                        (void *)reserve_start, (void *)reserve_end );
    }
}

/*
 * free_vma_list
 *
 * Free the buffer in the given VMA list.
 */
static void free_vma_list( struct vma_area_list *list )
{
    if (list->base)
        unmap_range_keep_reservations( list->base,
                                       ((unsigned char *)list->alloc_end -
                                        (unsigned char *)list->base) );
    list->base = NULL;
    list->list_end = NULL;
    list->alloc_end = NULL;
}

/*
 * alloc_scan_vma
 *
 * Parse /proc/self/maps into a newly allocated VMA area list.
 */
static void alloc_scan_vma( struct vma_area_list *listp )
{
    size_t max_count = page_size / sizeof(struct vma_area);
    struct vma_area_list vma_list;

    for (;;)
    {
        vma_list.base = wld_mmap( NULL, sizeof(struct vma_area) * max_count,
                                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                                  -1, 0 );
        if (vma_list.base == (struct vma_area *)-1)
            fatal_error( "could not allocate memory for VMA list\n");
        vma_list.list_end = vma_list.base;
        vma_list.alloc_end = vma_list.base + max_count;

        scan_vma( &vma_list, &max_count );
        if (vma_list.list_end - vma_list.base == max_count)
        {
            wld_memmove(listp, &vma_list, sizeof(*listp));
            break;
        }

        free_vma_list( &vma_list );
    }
}

/*
 * map_reserve_preload_ranges
 *
 * Attempt to reserve memory ranges into preload_info.
 * If any preload_info entry overlaps with stack, remove the entry instead of
 * reserving.
 */
static void map_reserve_preload_ranges( const struct vma_area_list *vma_list,
                                        const struct stackarg_info *stackinfo )
{
    size_t i;
    unsigned long exclude_start = (unsigned long)stackinfo->stack - 1;
    unsigned long exclude_end = (unsigned long)stackinfo->auxv + 1;

    for (i = 0; preload_info[i].size; i++)
    {
        if (exclude_end   >  (unsigned long)preload_info[i].addr &&
            exclude_start <= (unsigned long)preload_info[i].addr + preload_info[i].size - 1)
        {
            remove_preload_range( i );
            i--;
        }
        else if (map_reserve_unmapped_range( vma_list, preload_info[i].addr, preload_info[i].size ) < 0)
        {
            /* don't warn for low 64k */
            if (preload_info[i].addr >= (void *)0x10000
#ifdef __aarch64__
                && preload_info[i].addr < (void *)0x7fffffffff /* ARM64 address space might end here*/
#endif
            )
                wld_printf( "preloader: Warning: failed to reserve range %p-%p\n",
                            preload_info[i].addr, (char *)preload_info[i].addr + preload_info[i].size );
            remove_preload_range( i );
            i--;
        }
    }
}

/*
 * refresh_vma_and_reserve_preload_ranges
 *
 * Refresh the process VMA list, and try to reserve memory ranges in preload_info.
 */
static void refresh_vma_and_reserve_preload_ranges( struct vma_area_list *vma_list,
                                                    const struct stackarg_info *stackinfo )
{
    free_vma_list( vma_list );
    alloc_scan_vma( vma_list );
    map_reserve_preload_ranges( vma_list, stackinfo );
}

/*
 * stackargs_get_remap_policy
 *
 * Parse the remap policy value from the given environment variable.
 */
static enum remap_policy stackargs_get_remap_policy( const struct stackarg_info *info, const char *name,
                                                     enum remap_policy default_policy )
{
    char *valstr = stackargs_getenv( info, name ), *endptr;
    unsigned long valnum;

    if (valstr)
    {
        if (wld_strcmp(valstr, "auto") == 0 || wld_strcmp(valstr, "on-conflict") == 0)
            return REMAP_POLICY_ON_CONFLICT;
        if (wld_strcmp(valstr, "always") == 0 || wld_strcmp(valstr, "force") == 0)
            return REMAP_POLICY_FORCE;
        if (wld_strcmp(valstr, "never") == 0 || wld_strcmp(valstr, "skip") == 0)
            return REMAP_POLICY_SKIP;
        valnum = parse_ul( valstr, &endptr, 10, NULL );
        if (!*endptr && valnum < LAST_REMAP_POLICY) return valnum;
    }

    return default_policy;
}

/*
 * check_remap_policy
 *
 * Check remap policy against the given range and determine the action to take.
 *
 *  -1: fail
 *   0: do nothing
 *   1: proceed with remapping
 */
static int check_remap_policy( struct preloader_state *state,
                               const char *policy_envname, enum remap_policy default_policy,
                               unsigned long start, unsigned long size )
{
    switch (stackargs_get_remap_policy( &state->s, policy_envname, default_policy ))
    {
    case REMAP_POLICY_SKIP:
        return -1;
    case REMAP_POLICY_ON_CONFLICT:
        if (find_preload_reserved_area( (void *)start, size ) < 0)
            return 0;
        /* fallthrough */
    case REMAP_POLICY_FORCE:
    default:
        return 1;
    }
}

#ifndef __x86_64__
/*
 * remap_test_in_old_address_range
 *
 * Determine whether the address falls in the old mapping address range
 * (i.e. before mremap).
 */
static int remap_test_in_old_address_range( unsigned long address )
{
    return address - remap_test.old_mapping_start < remap_test.old_mapping_size;
}

/*
 * remap_test_signal_handler
 *
 * A signal handler that detects whether the kernel has acknowledged the new
 * addresss for the remapped vDSO.
 */
static void remap_test_signal_handler( int signum, siginfo_t *sinfo, void *context )
{
    (void)signum;
    (void)sinfo;
    (void)context;

    if (remap_test_in_old_address_range((unsigned long)__builtin_return_address(0))) goto fail;

#ifdef __i386__
    /* test for SYSENTER/SYSEXIT return address (int80_landing_pad) */
    if (remap_test_in_old_address_range(((ucontext_t *)context)->uc_mcontext.gregs[REG_EIP])) goto fail;
#endif

    remap_test.is_successful = 1;
    return;

fail:
    /* Kernel too old to support remapping. Restore vDSO/sigpage to return safely. */
    if (remap_test.delta) {
        if (remap_multiple_vmas( remap_test.vma_list, remap_test.delta, -1, 1 ) < 0)
            fatal_error( "Cannot restore remapped VMAs\n" );
        remap_test.delta = 0;
    }

    /* The signal handler might be called several times due to externally
     * originated spurious signals, so overwrite with the latest status just to
     * be safe.
     */
    remap_test.is_failed = 1;
}
#endif

/*
 * test_remap_successful
 *
 * Test if the kernel has acknowledged the remapped vDSO.
 *
 * Remapping vDSO requires explicit kernel support for most architectures, but
 * the support is missing in old Linux kernels (pre-4.8).  Among other things,
 * vDSO contains the default signal restorer (sigreturn trampoline) and the
 * fast syscall gate (SYSENTER) on Intel IA-32.  The kernel keeps track of
 * their addresses per process, and they need to be updated accordingly if the
 * vDSO address changes.  Without proper support, mremap() on vDSO does not
 * indicate failure, but the kernel still uses old addresses for the vDSO
 * components, resulting in crashes or other unpredictable behaviour if any of
 * those addresses are used.
 *
 * We attempt to detect this condition by installing a signal handler and
 * sending a signal to ourselves.  The signal handler will test if the restorer
 * address (plus the syscall gate on i386) falls in the old address range; if
 * this is the case, we remap the vDSO to its old address and report failure
 * (i.e. no support from kernel).  If the addresses do not overlap with the old
 * address range, the kernel is new enough to support vDSO remapping and we can
 * proceed as normal.
 */
static int test_remap_successful( struct vma_area_list *vma_list, struct preloader_state *state,
                                  unsigned long old_mapping_start, unsigned long old_mapping_size,
                                  unsigned long delta )
{
#ifdef __x86_64__
    (void)vma_list;
    (void)state;
    (void)old_mapping_start;
    (void)old_mapping_size;
    (void)delta;

    /* x86-64 doesn't use SYSENTER for syscalls, and requires sa_restorer for
     * signal handlers.  We can safely relocate vDSO without kernel support
     * (vdso_mremap).
     */
    return 0;
#else
    struct wld_sigaction sigact;
    pid_t pid;
    int result = -1;
    unsigned long syscall_addr = 0;

    pid = wld_getpid();
    if (pid < 0) fatal_error( "failed to get PID\n" );

#ifdef __i386__
    syscall_addr = get_auxiliary( state->s.auxv, AT_SYSINFO, 0 );
    if (syscall_addr - old_mapping_start < old_mapping_size) syscall_addr += delta;
#endif

    remap_test.old_mapping_start = old_mapping_start;
    remap_test.old_mapping_size = old_mapping_size;
    remap_test.vma_list = vma_list;
    remap_test.delta = delta;
    remap_test.is_successful = 0;
    remap_test.is_failed = 0;

    wld_memset( &sigact, 0, sizeof(sigact) );
    sigact.wld_sa_sigaction = remap_test_signal_handler;
    sigact.wld_sa_flags = WLD_SA_SIGINFO;
    /* We deliberately skip sa_restorer, since we're trying to get the address
     * of the kernel's built-in restorer function. */

    if (wld_sigaction( REMAP_TEST_SIG, &sigact, &sigact ) < 0) fatal_error( "cannot register test signal handler\n" );

    /* Unsafe region below - may race with signal handler */
#ifdef __i386__
    if (syscall_addr) {
        /* Also test __kernel_vsyscall return as well */
        __asm__ __volatile__( "call *%1"
                              : "=a" (result) : "r" (syscall_addr), "0" (37 /* SYS_kill */), "b" (pid), "c" (REMAP_TEST_SIG) );
        result = SYSCALL_RET(result);
    }
#else
    syscall_addr = 0;
#endif
    if (!syscall_addr) result = wld_kill( pid, REMAP_TEST_SIG );
    /* Unsafe region above - may race with signal handler */

    if (wld_sigaction( REMAP_TEST_SIG, &sigact, &sigact ) < 0) fatal_error( "cannot unregister test signal handler\n" );
    if (result == -1) fatal_error( "cannot raise test signal\n" );

    /* Now that the signal handler invocation is no longer possible, we can
     * safely access the result.
     *
     * If neither is_successful nor is_failed is set, it signifies that the
     * signal handler was not called or did not return properly.  In this case,
     * failure is assumed.
     *
     * If both is_successful and is_failed are set, it signifies that the
     * signal handler was called successively multiple times.  This may be due
     * to externally originated spurious signals.  In this case, is_failed
     * takes precedence.
     */
    if (remap_test.is_failed || !remap_test.is_successful) {
        if (remap_test.delta && remap_multiple_vmas( remap_test.vma_list, remap_test.delta, -1, 1 ) < 0)
            fatal_error( "Cannot restore remapped VMAs\n" );
        return -1;
    }

    return 0;
#endif
}

/*
 * remap_vdso
 *
 * Perform vDSO remapping if it conflicts with one of the reserved address ranges.
 */
static int remap_vdso( struct vma_area_list *vma_list, struct preloader_state *state )
{
    int result;
    unsigned long vdso_start, vdso_size, delta;
    void *new_vdso;
    struct wld_auxv *auxv;

    if (find_vma_envelope_range( vma_list, VMA_VDSO | VMA_VVAR, &vdso_start, &vdso_size ) < 0) return 0;

    result = check_remap_policy( state, "WINEPRELOADREMAPVDSO",
                                 REMAP_POLICY_DEFAULT_VDSO,
                                 vdso_start, vdso_size );
    if (result <= 0) return result;

    new_vdso = wld_mmap( NULL, vdso_size, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 );
    if (new_vdso == (void *)-1) return -1;

    delta = (unsigned long)new_vdso - vdso_start;
    /* It's easier to undo vvar remapping, so we remap it first. */
    if (remap_multiple_vmas( vma_list, delta, VMA_VVAR, 0 ) < 0 ||
        remap_multiple_vmas( vma_list, delta, VMA_VDSO, 0 ) < 0) goto remap_restore;

    /* NOTE: AArch32 may have restorer in vDSO if we're running on an old ARM64 kernel. */
    if (test_remap_successful( vma_list, state, vdso_start, vdso_size, delta ) < 0)
    {
        /* mapping restore done by test_remap_successful */
        return -1;
    }

    for (auxv = state->s.auxv; auxv->a_type != AT_NULL; auxv++)
    {
        switch (auxv->a_type)
        {
        case AT_SYSINFO:
        case AT_SYSINFO_EHDR:
            if ((unsigned long)auxv->a_un.a_val - vdso_start < vdso_size)
                auxv->a_un.a_val += delta;
            break;
        }
    }

    refresh_vma_and_reserve_preload_ranges( vma_list, &state->s );
    return 1;

remap_restore:
    if (remap_multiple_vmas( vma_list, delta, -1, 1 ) < 0)
        fatal_error( "Cannot restore remapped VMAs\n" );

    return -1;
}

/*
 *  wld_start
 *
 *  Repeat the actions the kernel would do when loading a dynamically linked .so
 *  Load the binary and then its ELF interpreter.
 *  Note, we assume that the binary is a dynamically linked ELF shared object.
 */
void* wld_start( void **stack )
{
    long i;
    char *interp, *reserve;
    struct wld_auxv new_av[8], delete_av[3];
    struct wld_link_map main_binary_map, ld_so_map;
    struct wine_preload_info **wine_main_preload_info;
    struct preloader_state state = { 0 };
    struct vma_area_list vma_list = { NULL };

    parse_stackargs( &state.s, *stack );

    if (state.s.argc < 2) fatal_error( "Usage: %s wine_binary [args]\n", state.s.argv[0] );

    page_size = get_auxiliary( state.s.auxv, AT_PAGESZ, 4096 );
    page_mask = page_size - 1;

    preloader_start = (char *)_start - ((unsigned long)_start & page_mask);
    preloader_end = (char *)((unsigned long)(_end + page_mask) & ~page_mask);

    if ((unsigned long)preloader_start >= (unsigned long)__executable_start + page_size)
    {
        /* Unmap preloader's ELF EHDR */
        unmap_range_keep_reservations( __executable_start,
                                       ((unsigned long)preloader_start -
                                        (unsigned long)__executable_start) & ~page_mask );
    }

#ifdef DUMP_AUX_INFO
    wld_printf( "stack = %p\n", state.s.stack );
    for( i = 0; i < state.s.argc; i++ ) wld_printf("argv[%lx] = %s\n", i, state.s.argv[i]);
    dump_auxiliary( state.s.auxv );
#endif

    /* reserve memory that Wine needs */
    reserve = stackargs_getenv( &state.s, "WINEPRELOADRESERVE" );
    if (reserve) preload_reserve( reserve );

    alloc_scan_vma( &vma_list );
    map_reserve_preload_ranges( &vma_list, &state.s );

    remap_vdso( &vma_list, &state );

    /* add an executable page at the top of the address space to defeat
     * broken no-exec protections that play with the code selector limit */
    if (find_preload_reserved_area( (char *)0x80000000 - page_size, page_size ) >= 0)
        wld_mprotect( (char *)0x80000000 - page_size, page_size, PROT_EXEC | PROT_READ );

    /* load the main binary */
    map_so_lib( state.s.argv[1], &main_binary_map );

    /* load the ELF interpreter */
    interp = (char *)main_binary_map.l_addr + main_binary_map.l_interp;
    map_so_lib( interp, &ld_so_map );

    /* store pointer to the preload info into the appropriate main binary variable */
    wine_main_preload_info = find_symbol( &main_binary_map, "wine_main_preload_info", STT_OBJECT );
    if (wine_main_preload_info) *wine_main_preload_info = preload_info;
    else wld_printf( "wine_main_preload_info not found\n" );

#define SET_NEW_AV(n,type,val) new_av[n].a_type = (type); new_av[n].a_un.a_val = (val);
    SET_NEW_AV( 0, AT_PHDR, (unsigned long)main_binary_map.l_phdr );
    SET_NEW_AV( 1, AT_PHENT, sizeof(ElfW(Phdr)) );
    SET_NEW_AV( 2, AT_PHNUM, main_binary_map.l_phnum );
    SET_NEW_AV( 3, AT_PAGESZ, page_size );
    SET_NEW_AV( 4, AT_BASE, ld_so_map.l_addr );
    SET_NEW_AV( 5, AT_FLAGS, get_auxiliary( state.s.auxv, AT_FLAGS, 0 ) );
    SET_NEW_AV( 6, AT_ENTRY, main_binary_map.l_entry );
    SET_NEW_AV( 7, AT_NULL, 0 );
#undef SET_NEW_AV

    i = 0;
    /* delete sysinfo values if addresses conflict and remap failed */
    if (is_in_preload_range( state.s.auxv, AT_SYSINFO ) || is_in_preload_range( state.s.auxv, AT_SYSINFO_EHDR ))
    {
        delete_av[i++].a_type = AT_SYSINFO;
        delete_av[i++].a_type = AT_SYSINFO_EHDR;
    }
    delete_av[i].a_type = AT_NULL;

    /* get rid of first argument */
    set_process_name( state.s.argc, state.s.argv );
    stackargs_shift_args( &state.s, 1 );

    *stack = state.s.stack;
    set_auxiliary_values( state.s.auxv, new_av, delete_av, stack );
    /* state is invalid from this point onward */

#ifdef DUMP_AUX_INFO
    wld_printf("new stack = %p\n", *stack);
    wld_printf("jumping to %p\n", (void *)ld_so_map.l_entry);
#endif
#ifdef DUMP_MAPS
    {
        char buffer[1024];
        int len, fd = wld_open( "/proc/self/maps", O_RDONLY );
        if (fd != -1)
        {
            while ((len = wld_read( fd, buffer, sizeof(buffer) )) > 0) wld_write( 2, buffer, len );
            wld_close( fd );
        }
    }
#endif

    free_vma_list( &vma_list );

    return (void *)ld_so_map.l_entry;
}

#endif /* __linux__ */
