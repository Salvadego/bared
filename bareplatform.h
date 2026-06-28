/*
 * bareplatform.h - operating system detection and platform primitives
 * ======================================================================
 *
 *  USAGE
 *    #define BAREPLATFORM_IMPLEMENTATION
 *    #include "bareplatform.h"
 *
 *  DEPENDS ON
 *    barecompilers.h
 *
 *  SCOPE
 *    This is the only "bare" header allowed to include an OS header
 *    (<windows.h>, <sys/mman.h>, <unistd.h>, <pthread.h>, and so on).
 *    Every other header, including barestd.h, depends on this one for
 *    anything that differs between Windows, macOS, and Linux, and
 *    never branches on _WIN32 / __APPLE__ / __linux__ itself. The
 *    payoff of that rule is what you are looking at right now: nothing
 *    below this comment block has a visible #ifdef once you are
 *    calling these functions from outside this file. The #ifdefs still
 *    exist -- the differences they paper over are real -- they are
 *    just all confined to one place instead of scattered through every
 *    header that happens to need a page of memory or a mutex.
 *
 *  MODULES
 *    vm_*      virtual memory: reserve / commit / decommit / release
 *    bare_tls_*  thread-local pointer slot, for code that cannot use
 *              BARE_THREAD_LOCAL directly (e.g. because the slot count
 *              is only known at runtime)
 */
#ifndef BAREPLATFORM_H
#define BAREPLATFORM_H

#include "baredefs.h"
#include "barecompilers.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ----------------------------------------------------------------
 *  Operating system identification
 * ---------------------------------------------------------------- */
#if defined(_WIN32) || defined(_WIN64)
#        define BARE_OS_WINDOWS 1
#elif defined(__APPLE__)
#        define BARE_OS_MACOS 1
#elif defined(__linux__)
#        define BARE_OS_LINUX 1
#else
#        define BARE_OS_UNKNOWN 1
#endif

#ifndef BARE_OS_WINDOWS
#        define BARE_OS_WINDOWS 0
#endif
#ifndef BARE_OS_MACOS
#        define BARE_OS_MACOS 0
#endif
#ifndef BARE_OS_LINUX
#        define BARE_OS_LINUX 0
#endif
#ifndef BARE_OS_UNKNOWN
#        define BARE_OS_UNKNOWN 0
#endif

#define BARE_OS_POSIX (BARE_OS_MACOS || BARE_OS_LINUX)

/*
 * vm_page_size - query the platform's page size
 *
 * Returns the number of bytes in one virtual-memory page on this
 * machine. Every size and address passed to vm_reserve(), vm_commit(),
 * vm_decommit(), and vm_release() must be a multiple of this value;
 * those functions assert that requirement rather than rounding for
 * you, so that a caller's own accounting can never silently drift
 * from what actually happened.
 *
 * The page size is a property of the running machine, not of the
 * binary, and in principle could differ between two runs on different
 * hardware (4 KiB is typical on x86-64; some ARM systems use 16 KiB
 * or 64 KiB pages). Call this function and use its result; never
 * hard-code 4096.
 *
 * Return: the page size in bytes. Always a power of two, always
 * greater than zero.
 */
BAREDEF size_t vm_page_size(void);

/*
 * vm_reserve - reserve a range of virtual address space
 * @size: number of bytes to reserve; must be > 0 and a multiple of
 *        vm_page_size()
 *
 * Reserves @size bytes of address space without backing any of it
 * with physical memory. This is the cheap, large-scale operation: a
 * multi-gigabyte reservation costs only page-table bookkeeping, not
 * gigabytes of RAM. Use this to claim a big address range up front
 * (for an arena's maximum possible size, say) and commit only the
 * prefix you are actually using as you grow into it.
 *
 * The memory returned is not yet safe to read or write; doing so
 * before a matching vm_commit() covering that address is undefined
 * behavior, the same as touching unmapped memory.
 *
 * Return: a page-aligned pointer to the start of the reserved range,
 * or NULL if the reservation could not be satisfied.
 */
BAREDEF void* vm_reserve(size_t size);

/*
 * vm_commit - back part of a reservation with physical memory
 * @addr: a page-aligned address inside a range previously returned by
 *        vm_reserve() and not yet released
 * @size: number of bytes to commit, starting at @addr; must be > 0
 *        and a multiple of vm_page_size()
 *
 * After this call succeeds, the byte range [@addr, @addr + @size) is
 * safe to read and write. Newly committed memory always reads as
 * zero before anything is written to it, matching both POSIX
 * mmap(MAP_ANONYMOUS) and Win32 VirtualAlloc(MEM_COMMIT) -- you do not
 * need to memset a freshly committed range yourself.
 *
 * Return: true on success, false on failure (commit failed, e.g. the
 * system is out of physical memory or overcommit limits).
 */
BAREDEF bool vm_commit(void* addr, size_t size);

/*
 * vm_decommit - release the physical backing of part of a reservation
 * @addr: a page-aligned address inside a previously committed range
 * @size: number of bytes to decommit; must be > 0 and a multiple of
 *        vm_page_size()
 *
 * Frees the physical memory backing [@addr, @addr + @size) while
 * leaving the address range itself reserved -- vm_release() is still
 * required later to give the address space back. After this call,
 * touching the decommitted range without a fresh vm_commit() is
 * undefined behavior; the platform is free to make this an immediate,
 * loud fault rather than a silent zero-fill, and this function is
 * implemented so that it always is one, on every supported platform.
 *
 * Return: true on success, false on failure.
 */
BAREDEF bool vm_decommit(void* addr, size_t size);

/*
 * vm_release - give an entire reservation back to the operating system
 * @addr: the exact pointer previously returned by vm_reserve()
 * @size: the exact size previously passed to that vm_reserve() call
 *
 * Releases the whole reservation @addr was returned from, committed
 * portions included. After this call, @addr is no longer valid for
 * any purpose: not for reading, not for writing, not for another
 * vm_commit(). This is the inverse of vm_reserve(), not of vm_commit()
 * -- there is no partial-release operation, by design, because a
 * partially released reservation is exactly the kind of state that
 * silently corrupts whatever else the address space allocator might
 * decide to put there next.
 *
 * Return: true on success, false on failure.
 */
BAREDEF bool vm_release(void* addr, size_t size);

#endif /* BAREPLATFORM_H */

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREPLATFORM_IMPLEMENTATION
#ifndef BAREPLATFORM_IMPLEMENTATION_DONE
#define BAREPLATFORM_IMPLEMENTATION_DONE

#if BARE_OS_WINDOWS
#        ifndef WIN32_LEAN_AND_MEAN
#                define WIN32_LEAN_AND_MEAN
#        endif
#        include <windows.h>
#elif BARE_OS_POSIX
/*
 * _DEFAULT_SOURCE must be visible before the FIRST system header in
 * the whole translation unit, because glibc latches its feature-test
 * visibility decision on that first include and never revisits it
 * afterward -- defining it later, even right here, has no effect if
 * any other header (the user's own code, a different "bare" header)
 * already pulled in e.g. <stdio.h> first. Confining this to
 * bareplatform.h's implementation block, and asking users to include
 * bareplatform.h before anything else when they need
 * BAREPLATFORM_IMPLEMENTATION, is the most we can do about that from
 * inside a single header; the syscall declared by hand below is the
 * actual fix and works regardless of include order.
 */
#        if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#                define _DEFAULT_SOURCE 1
#        endif
#        include <sys/mman.h>
#        include <unistd.h>
#endif

size_t vm_page_size(void) {
#if BARE_OS_WINDOWS
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        size_t page = (size_t)info.dwPageSize;
#else
        long raw = sysconf(_SC_PAGESIZE);
        Assert(raw > 0, "vm_page_size: sysconf(_SC_PAGESIZE) must succeed");
        size_t page = (size_t)raw;
#endif
        Assert(page > 0, "vm_page_size: page size must be positive");
        Assert(IsPow2(page), "vm_page_size: page size must be a power of two");
        return page;
}

void* vm_reserve(size_t size) {
        Assert(size > 0, "vm_reserve: size must be positive");
        Assert(size % vm_page_size() == 0,
               "vm_reserve: size must be a multiple of the page size");

#if BARE_OS_WINDOWS
        void* p = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
#else
        void* p = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) p = NULL;
#endif
        AssertImplies(p != NULL,
                      (uintptr_t)p % vm_page_size() == 0,
                      "vm_reserve: OS returned a non-page-aligned address");
        return p;
}

bool vm_commit(void* addr, size_t size) {
        Assert(addr != NULL, "vm_commit: addr must not be NULL");
        Assert(size > 0, "vm_commit: size must be positive");
        Assert((uintptr_t)addr % vm_page_size() == 0,
               "vm_commit: addr must be page-aligned");
        Assert(size % vm_page_size() == 0,
               "vm_commit: size must be a multiple of the page size");

#if BARE_OS_WINDOWS
        return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
        return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

bool vm_decommit(void* addr, size_t size) {
        Assert(addr != NULL, "vm_decommit: addr must not be NULL");
        Assert(size > 0, "vm_decommit: size must be positive");
        Assert((uintptr_t)addr % vm_page_size() == 0,
               "vm_decommit: addr must be page-aligned");
        Assert(size % vm_page_size() == 0,
               "vm_decommit: size must be a multiple of the page size");

#if BARE_OS_WINDOWS
        return VirtualFree(addr, size, MEM_DECOMMIT) != 0;
#else
        /*
         * madvise(MADV_DONTNEED) drops the physical backing eagerly;
         * mprotect(PROT_NONE) makes touching the range fault
         * immediately instead of silently zero-filling on next
         * access, matching Win32's MEM_DECOMMIT contract.
         *
         * We declare madvise() ourselves rather than rely on glibc's
         * <sys/mman.h> declaration of it: glibc only exposes that
         * declaration when a feature-test macro was visible before
         * the FIRST system header of the entire translation unit, a
         * condition this header cannot guarantee for its callers (see
         * the _DEFAULT_SOURCE comment above). The kernel ABI behind
         * madvise(2) is stable and documented independently of any
         * libc's opt-in visibility macros, so declaring the wrapper
         * by hand is the actually-portable fix, not a workaround.
         */
#        ifndef BARE_MADV_DONTNEED
#                define BARE_MADV_DONTNEED 4
#        endif
        extern int madvise(void* addr, size_t length, int advice);
        int rc1 = madvise(addr, size, BARE_MADV_DONTNEED);
        int rc2 = mprotect(addr, size, PROT_NONE);
        return rc1 == 0 && rc2 == 0;
#endif
}

bool vm_release(void* addr, size_t size) {
        Assert(addr != NULL, "vm_release: addr must not be NULL");
        Assert(size > 0, "vm_release: size must be positive");
        Assert((uintptr_t)addr % vm_page_size() == 0,
               "vm_release: addr must be page-aligned");

#if BARE_OS_WINDOWS
        Unused(size); /* VirtualFree(MEM_RELEASE) wants only the original base */
        return VirtualFree(addr, 0, MEM_RELEASE) != 0;
#else
        return munmap(addr, size) == 0;
#endif
}

#endif /* BAREPLATFORM_IMPLEMENTATION_DONE */
#endif /* BAREPLATFORM_IMPLEMENTATION */
