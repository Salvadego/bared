/*
 * os.h - Portable filesystem and process interface
 * ==================================================
 *
 *  USAGE
 *    #define BAREOS_IMPLEMENTATION
 *    #include "bareos.h"
 *
 *  DEPENDS ON
 *    baretime.h (time inclusion)
 *    barestd.h  (Arena, Str, Slice)
 *
 *  DESIGN
 *    FileKind is an enum covering all known Unix file types plus
 *    Windows-specific entries.  FileInfo holds a tagged union: the
 *    `meta` field is only valid for BLOCK_DEVICE, CHAR_DEVICE, and
 *    SYMLINK kinds - all other kinds have no extra metadata.
 *
 *    OsOpenFlags is a portable bitmask.  Internally it maps to
 *    O_RDONLY / O_CREAT / ... on POSIX and CreateFile flags on Windows.
 *
 *    File holds a raw descriptor (fd on POSIX, HANDLE on Windows)
 *    and eagerly-populated FileInfo.  fd == -1 means invalid/closed.
 *
 *  EXAMPLE
 *
 *    Arena *a    = arena_new(MB(1));
 *    File   f    = os_open(a, str_lit("data.txt"), OS_RDONLY);
 *    FileInfo fi = f.info;
 *    printf("size=%lld kind=%d\n", (long long)fi.size, fi.kind);
 *    os_close(&f);
 *
 *    Slice(FileInfo) entries = os_readdir(a, str_lit("."));
 *    size_t i;
 *    for (i = 0; i < slice_len(entries); i++)
 *        printf(StrFmt "\n", StrArgs(entries[i].name));
 */

#ifndef BAREOS_H
#define BAREOS_H

#include <stdbool.h>
#include <stdint.h>

#include "barestd.h"
#include "baretime.h"

/* ================================================================
 *  FileKind - covers all known Unix and Windows file types
 * ================================================================ */

typedef enum {
        FILE_KIND_UNKNOWN = 0,
        FILE_KIND_REGULAR,      /* ordinary file                    */
        FILE_KIND_DIRECTORY,    /* directory                        */
        FILE_KIND_SYMLINK,      /* symbolic link                    */
        FILE_KIND_BLOCK_DEVICE, /* block device  (Unix)             */
        FILE_KIND_CHAR_DEVICE,  /* character device (Unix)          */
        FILE_KIND_NAMED_PIPE,   /* FIFO / named pipe                */
        FILE_KIND_UNIX_SOCKET,  /* Unix domain socket               */
        FILE_KIND_DOOR,         /* Solaris door                     */
        FILE_KIND_EVENT_PORT,   /* Solaris event port               */
        FILE_KIND_WHITEOUT,     /* BSD overlay-fs whiteout          */
} FileKind;

/* ================================================================
 *  FileInfo - kind + stat fields + tagged union for extra metadata
 * ================================================================ */

typedef struct {
        uint32_t major;
        uint32_t minor;
} DeviceInfo;

typedef struct {
        Str target; /* path the symlink points to (arena-owned) */
} SymlinkInfo;

typedef struct {
        FileKind kind;

        Str      name;     /* filename only (not full path)    */
        int64_t  size;     /* bytes; 0 for non-regular files   */
        int64_t  mtime_ns; /* last modification (nanoseconds)  */
        int64_t  atime_ns; /* last access      (nanoseconds)   */
        int64_t  ctime_ns; /* last status change / creation    */
        uint32_t mode;     /* permission bits (0 on Windows)   */
        uint32_t uid;      /* owner user ID   (0 on Windows)   */
        uint32_t gid;      /* owner group ID  (0 on Windows)   */

        /* Extra metadata - only valid for matching kinds */
        union {
                DeviceInfo  device;  /* FILE_KIND_BLOCK_DEVICE / CHAR_DEVICE */
                SymlinkInfo symlink; /* FILE_KIND_SYMLINK                     */
        } meta;
} FileInfo;

/* ================================================================
 *  OsOpenFlags - portable bitmask
 * ================================================================ */

typedef uint32_t OsOpenFlags;

#define OS_RDONLY ((OsOpenFlags)0x0001)
#define OS_WRONLY ((OsOpenFlags)0x0002)
#define OS_RDWR   ((OsOpenFlags)0x0003)
#define OS_APPEND ((OsOpenFlags)0x0010)
#define OS_CREATE ((OsOpenFlags)0x0020) /* create if not exists */
#define OS_TRUNC  ((OsOpenFlags)0x0040) /* truncate on open     */
#define OS_EXCL   ((OsOpenFlags)0x0080) /* fail if exists       */

/* ================================================================
 *  File - open file descriptor + eagerly-populated info
 * ================================================================ */

#if defined(_WIN32) || defined(_WIN64)
#        include <windows.h>
typedef HANDLE OsFd;
#        define OS_INVALID_FD INVALID_HANDLE_VALUE
#else
#        define OS_INVALID_FD (-1)
typedef int OsFd;
#endif

typedef struct {
        OsFd     fd;   /* raw descriptor - OS_INVALID_FD when closed */
        FileInfo info; /* eagerly populated at open time             */
        char*    path; /* arena-owned NUL-terminated path            */
} File;

/* ================================================================
 *  API
 * ================================================================ */

/* Open an existing file.  Asserts on failure. */
File os_open(Arena* a, Str path, OsOpenFlags flags);

/* Create or truncate a file.  mode is the Unix permission bits
   (e.g. 0644); ignored on Windows. */
File os_create(Arena* a, Str path, uint32_t mode);

/* Close the file descriptor.  Sets fd to OS_INVALID_FD. */
void os_close(File* f);

/* Stat a path without opening it.  Follows symlinks. */
FileInfo os_stat(Arena* a, Str path);

/* Stat without following symlinks. */
FileInfo os_lstat(Arena* a, Str path);

/* Read entire file contents into arena. */
Str os_read_file(Arena* a, Str path);

/* Write data to path (create or truncate). Returns true on success. */
bool os_write_file(Str path, Str data);

/* Path predicate helpers. */
bool os_exists(Str path);
bool os_is_dir(Str path);
bool os_is_file(Str path);

/* Directory operations. */
bool os_mkdir(Str path, uint32_t mode);               /* single directory     */
bool os_mkdir_all(Arena* a, Str path, uint32_t mode); /* like mkdir -p */
bool os_remove(Str path);                             /* file or empty dir    */
bool os_rename(Str from, Str to);

/* Read directory entries (does NOT include "." and "..").
   FileInfo.name is arena-owned.  Symlink targets are populated. */
Slice(FileInfo) os_readdir(Arena* a, Str path);

/* Follow a symlink and return the target path. */
Str os_readlink(Arena* a, Str path);

/* Process / environment. */
Str os_getenv(Arena* a, Str key);
Str os_getcwd(Arena* a);

/* Returns arena-owned Slice(Str) of paths matching pattern.
   Pattern follows shell glob rules: * ? [abc]
   Requires _POSIX_C_SOURCE >= 200809L (already set by bareos.h). */
Slice(Str) os_glob(Arena* a, Str pattern);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREOS_IMPLEMENTATION

#        include <stdio.h>
#        include <stdlib.h>
#        include <string.h>

/* ----------------------------------------------------------------
 *  POSIX implementation
 * ---------------------------------------------------------------- */
#        if !defined(_WIN32) && !defined(_WIN64)

/* _POSIX_C_SOURCE 200809L: lstat, readlink, S_IF* constants, st_mtim.
   Must be set before any system header - if btime.h was included first
   it already set this to >= 200809L. */
#                if !defined(_WIN32) && !defined(_WIN64)
#                        if !defined(_POSIX_C_SOURCE) || \
                            _POSIX_C_SOURCE < 200809L
#                                undef _POSIX_C_SOURCE
#                                define _POSIX_C_SOURCE 200809L
#                        endif
#                        if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#                                define _DEFAULT_SOURCE 1
#                        endif
#                endif

#                include <dirent.h>
#                include <errno.h>
#                include <fcntl.h>
#                include <sys/stat.h>
#                include <sys/types.h>
#                include <unistd.h>
/* major() / minor() live in <sys/sysmacros.h> on Linux,
   in <sys/types.h> on macOS/BSD - include both defensively. */
#                if defined(__linux__)
#                        include <sys/sysmacros.h>
#                endif

/* Map platform S_IF* -> FileKind */
static FileKind _kind_from_mode(mode_t m) {
        switch (m & S_IFMT) {
                case S_IFREG:
                        return FILE_KIND_REGULAR;
                case S_IFDIR:
                        return FILE_KIND_DIRECTORY;
                case S_IFLNK:
                        return FILE_KIND_SYMLINK;
                case S_IFBLK:
                        return FILE_KIND_BLOCK_DEVICE;
                case S_IFCHR:
                        return FILE_KIND_CHAR_DEVICE;
                case S_IFIFO:
                        return FILE_KIND_NAMED_PIPE;
                case S_IFSOCK:
                        return FILE_KIND_UNIX_SOCKET;
#                ifdef S_IFDOOR
                case S_IFDOOR:
                        return FILE_KIND_DOOR;
#                endif
#                ifdef S_IFPORT
                case S_IFPORT:
                        return FILE_KIND_EVENT_PORT;
#                endif
#                ifdef S_IFWHT
                case S_IFWHT:
                        return FILE_KIND_WHITEOUT;
#                endif
                default:
                        return FILE_KIND_UNKNOWN;
        }
}

static FileInfo _stat_to_info(Arena* a, const struct stat* s, Str name) {
        FileInfo fi;
        memset(&fi, 0, sizeof fi);
        fi.kind = _kind_from_mode(s->st_mode);
        fi.name = name;
        fi.size = (int64_t)s->st_size;
        fi.mode = (uint32_t)(s->st_mode & 07777);
        fi.uid  = (uint32_t)s->st_uid;
        fi.gid  = (uint32_t)s->st_gid;

#                if defined(__APPLE__) || defined(__MACH__)
        fi.mtime_ns = (int64_t)s->st_mtimespec.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_mtimespec.tv_nsec;
        fi.atime_ns = (int64_t)s->st_atimespec.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_atimespec.tv_nsec;
        fi.ctime_ns = (int64_t)s->st_ctimespec.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_ctimespec.tv_nsec;
#                else
        fi.mtime_ns = (int64_t)s->st_mtim.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_mtim.tv_nsec;
        fi.atime_ns = (int64_t)s->st_atim.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_atim.tv_nsec;
        fi.ctime_ns = (int64_t)s->st_ctim.tv_sec * (int64_t)1000000000LL +
                      (int64_t)s->st_ctim.tv_nsec;
#                endif

        if (fi.kind == FILE_KIND_BLOCK_DEVICE ||
            fi.kind == FILE_KIND_CHAR_DEVICE) {
                fi.meta.device.major = (uint32_t)major(s->st_rdev);
                fi.meta.device.minor = (uint32_t)minor(s->st_rdev);
        }
        if (fi.kind == FILE_KIND_SYMLINK && a) {
                fi.meta.symlink.target = os_readlink(a, name);
        }
        return fi;
}

static int _posix_flags(OsOpenFlags f) {
        int pf = 0;
        int rw = (int)(f & OS_RDWR);
        if (rw == (int)OS_RDWR)
                pf |= O_RDWR;
        else if (rw == (int)OS_WRONLY)
                pf |= O_WRONLY;
        else
                pf |= O_RDONLY;
        if (f & OS_APPEND) pf |= O_APPEND;
        if (f & OS_CREATE) pf |= O_CREAT;
        if (f & OS_TRUNC) pf |= O_TRUNC;
        if (f & OS_EXCL) pf |= O_EXCL;
        return pf;
}

File os_open(Arena* a, Str path, OsOpenFlags flags) {
        char* cpath = str_to_cstr(a, path);
        int   pf    = _posix_flags(flags);
        int   fd    = open(cpath, pf, 0666);
        assert(fd >= 0 && "os_open: failed to open file");

        struct stat s;
        fstat(fd, &s);

        File f;
        f.fd   = fd;
        f.path = cpath;
        f.info = _stat_to_info(a, &s, path);
        return f;
}

File os_create(Arena* a, Str path, uint32_t mode) {
        char* cpath = str_to_cstr(a, path);
        int   fd    = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)mode);
        assert(fd >= 0 && "os_create: failed to create file");

        struct stat s;
        fstat(fd, &s);

        File f;
        f.fd   = fd;
        f.path = cpath;
        f.info = _stat_to_info(a, &s, path);
        return f;
}

void os_close(File* f) {
        if (f->fd != OS_INVALID_FD) {
                close(f->fd);
                f->fd = OS_INVALID_FD;
        }
}

FileInfo os_stat(Arena* a, Str path) {
        char        cpath[4096];
        size_t      l = path.len < 4095 ? path.len : 4095;
        struct stat s;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        assert(stat(cpath, &s) == 0 && "os_stat: failed");
        return _stat_to_info(a, &s, path);
}

FileInfo os_lstat(Arena* a, Str path) {
        char        cpath[4096];
        size_t      l = path.len < 4095 ? path.len : 4095;
        struct stat s;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        assert(lstat(cpath, &s) == 0 && "os_lstat: failed");
        return _stat_to_info(a, &s, path);
}

Str os_read_file(Arena* a, Str path) {
        File f   = os_open(a, path, OS_RDONLY);
        Str  out = str_null();
        if (f.info.size > 0) {
                char*   buf = arena_push_array(a, char, (size_t)f.info.size);
                ssize_t got = read(f.fd, buf, (size_t)f.info.size);
                assert(got >= 0 && "os_read_file: read failed");
                out = str_buf(buf, (size_t)got);
        }
        os_close(&f);
        return out;
}

bool os_write_file(Str path, Str data) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        int fd   = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return false;
        ssize_t w = write(fd, data.ptr, data.len);
        close(fd);
        return (size_t)w == data.len;
}

bool os_exists(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return access(cpath, F_OK) == 0;
}

bool os_is_dir(Str path) {
        char        cpath[4096];
        size_t      l = path.len < 4095 ? path.len : 4095;
        struct stat s;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return stat(cpath, &s) == 0 && S_ISDIR(s.st_mode);
}

bool os_is_file(Str path) {
        char        cpath[4096];
        size_t      l = path.len < 4095 ? path.len : 4095;
        struct stat s;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return stat(cpath, &s) == 0 && S_ISREG(s.st_mode);
}

bool os_mkdir(Str path, uint32_t mode) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return mkdir(cpath, (mode_t)mode) == 0;
}

bool os_mkdir_all(Arena* a, Str path, uint32_t mode) {
        char*  buf = str_to_cstr(a, path);
        size_t i;
        for (i = 1; buf[i]; i++) {
                if (buf[i] == '/') {
                        buf[i] = '\0';
                        mkdir(buf,
                              (mode_t)mode); /* ignore errors - may exist */
                        buf[i] = '/';
                }
        }
        return mkdir(buf, (mode_t)mode) == 0 || errno == EEXIST;
}

bool os_remove(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return remove(cpath) == 0;
}

bool os_rename(Str from, Str to) {
        char   cf[4096], ct[4096];
        size_t lf = from.len < 4095 ? from.len : 4095;
        size_t lt = to.len < 4095 ? to.len : 4095;
        memcpy(cf, from.ptr, lf);
        cf[lf] = '\0';
        memcpy(ct, to.ptr, lt);
        ct[lt] = '\0';
        return rename(cf, ct) == 0;
}

Str os_readlink(Arena* a, Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l]    = '\0';
        char*   buf = arena_push_array(a, char, 4096);
        ssize_t n   = readlink(cpath, buf, 4095);
        if (n < 0) return str_null();
        buf[n] = '\0';
        return str_buf(buf, (size_t)n);
}

Slice(FileInfo) os_readdir(Arena* a, Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';

        DIR* dir = opendir(cpath);
        assert(dir && "os_readdir: failed to open directory");

        Slice(FileInfo) entries = slice_make(a, FileInfo, 16);
        struct dirent* ent;

        while ((ent = readdir(dir)) != NULL) {
                if (ent->d_name[0] == '.' &&
                    (ent->d_name[1] == '\0' ||
                     (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
                        continue; /* skip . and .. */

                /* Build full path for stat */
                size_t nlen  = strlen(ent->d_name);
                size_t plen  = l + 1 + nlen;
                char*  fpath = arena_push_array(a, char, plen + 1);
                memcpy(fpath, cpath, l);
                fpath[l] = '/';
                memcpy(fpath + l + 1, ent->d_name, nlen);
                fpath[plen] = '\0';

                struct stat s;
                if (lstat(fpath, &s) != 0) continue;

                Str      name = str_clone(a, str_from_c(ent->d_name));
                FileInfo fi   = _stat_to_info(a, &s, name);

                /* Resolve symlink target */
                if (fi.kind == FILE_KIND_SYMLINK) {
                        char    tbuf[4096];
                        ssize_t tn = readlink(fpath, tbuf, 4095);
                        if (tn >= 0) {
                                tbuf[tn] = '\0';
                                fi.meta.symlink.target =
                                    str_clone(a, str_from_c(tbuf));
                        }
                }

                slice_push(entries, FileInfo, fi);
        }
        closedir(dir);
        return entries;
}

Str os_getenv(Arena* a, Str key) {
        char* ckey = str_to_cstr(a, key);
        char* val  = getenv(ckey);
        if (!val) return str_null();
        return str_clone(a, str_from_c(val));
}

Str os_getcwd(Arena* a) {
        char* buf = arena_push_array(a, char, 4096);
        char* r   = getcwd(buf, 4096);
        assert(r && "os_getcwd: failed");
        return str_from_c(buf);
}

/* ----------------------------------------------------------------
 *  Windows implementation
 * ---------------------------------------------------------------- */
#        else /* _WIN32 || _WIN64 */

#                include <fileapi.h>
#                include <handleapi.h>
#                include <io.h>
#                include <windows.h>

static int64_t _filetime_to_ns(FILETIME ft) {
        ULARGE_INTEGER u;
        u.LowPart  = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        /* 100-ns intervals since Jan 1 1601; convert to ns */
        return (int64_t)(u.QuadPart * 100LL);
}

static FileKind _win_attr_to_kind(DWORD attr, DWORD reparseTag) {
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (reparseTag == IO_REPARSE_TAG_SYMLINK)
                        return FILE_KIND_SYMLINK;
                return FILE_KIND_UNKNOWN;
        }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) return FILE_KIND_DIRECTORY;
        return FILE_KIND_REGULAR;
}

static FileInfo _win_data_to_info(Arena* a, WIN32_FIND_DATAW* fd, Str name) {
        FileInfo fi;
        memset(&fi, 0, sizeof fi);
        fi.kind     = _win_attr_to_kind(fd->dwFileAttributes, fd->dwReserved0);
        fi.name     = name;
        fi.size     = ((int64_t)fd->nFileSizeHigh << 32) | fd->nFileSizeLow;
        fi.mtime_ns = _filetime_to_ns(fd->ftLastWriteTime);
        fi.atime_ns = _filetime_to_ns(fd->ftLastAccessTime);
        fi.ctime_ns = _filetime_to_ns(fd->ftCreationTime);
        Unused(a);
        return fi;
}

File os_open(Arena* a, Str path, OsOpenFlags flags) {
        char* cpath  = str_to_cstr(a, path);
        DWORD access = 0, share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD create = OPEN_EXISTING;
        if ((flags & OS_RDWR) == OS_RDWR)
                access = GENERIC_READ | GENERIC_WRITE;
        else if (flags & OS_WRONLY)
                access = GENERIC_WRITE;
        else
                access = GENERIC_READ;
        if (flags & OS_CREATE) create = OPEN_ALWAYS;
        if (flags & OS_TRUNC) create = CREATE_ALWAYS;

        HANDLE h = CreateFileA(
            cpath, access, share, NULL, create, FILE_ATTRIBUTE_NORMAL, NULL);
        assert(h != INVALID_HANDLE_VALUE && "os_open: failed");

        BY_HANDLE_FILE_INFORMATION info;
        GetFileInformationByHandle(h, &info);

        FileInfo fi;
        memset(&fi, 0, sizeof fi);
        fi.kind     = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                          ? FILE_KIND_DIRECTORY
                          : FILE_KIND_REGULAR;
        fi.name     = path;
        fi.size     = ((int64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        fi.mtime_ns = _filetime_to_ns(info.ftLastWriteTime);
        fi.atime_ns = _filetime_to_ns(info.ftLastAccessTime);
        fi.ctime_ns = _filetime_to_ns(info.ftCreationTime);

        File f;
        f.fd   = h;
        f.path = cpath;
        f.info = fi;
        return f;
}

File os_create(Arena* a, Str path, uint32_t mode) {
        Unused(mode);
        return os_open(a, path, OS_WRONLY | OS_CREATE | OS_TRUNC);
}

void os_close(File* f) {
        if (f->fd != OS_INVALID_FD) {
                CloseHandle(f->fd);
                f->fd = OS_INVALID_FD;
        }
}

FileInfo os_stat(Arena* a, Str path) {
        WIN32_FIND_DATAA fd;
        char*            cpath = str_to_cstr(a, path);
        HANDLE           h     = FindFirstFileA(cpath, (WIN32_FIND_DATAA*)&fd);
        assert(h != INVALID_HANDLE_VALUE && "os_stat: failed");
        FindClose(h);
        WIN32_FIND_DATAW fdw;
        memset(&fdw, 0, sizeof fdw);
        fdw.dwFileAttributes = fd.dwFileAttributes;
        fdw.ftLastWriteTime  = fd.ftLastWriteTime;
        fdw.ftLastAccessTime = fd.ftLastAccessTime;
        fdw.ftCreationTime   = fd.ftCreationTime;
        fdw.nFileSizeHigh    = fd.nFileSizeHigh;
        fdw.nFileSizeLow     = fd.nFileSizeLow;
        fdw.dwReserved0      = fd.dwReserved0;
        return _win_data_to_info(a, &fdw, path);
}

FileInfo os_lstat(Arena* a, Str path) {
        /* Windows does not distinguish lstat from stat cleanly */
        return os_stat(a, path);
}

Str os_read_file(Arena* a, Str path) {
        File  f   = os_open(a, path, OS_RDONLY);
        DWORD got = 0;
        char* buf = arena_push_array(a, char, (size_t)f.info.size);
        ReadFile(f.fd, buf, (DWORD)f.info.size, &got, NULL);
        os_close(&f);
        return str_buf(buf, (size_t)got);
}

bool os_write_file(Str path, Str data) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        HANDLE h = CreateFileA(cpath,
                               GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0;
        WriteFile(h, data.ptr, (DWORD)data.len, &w, NULL);
        CloseHandle(h);
        return (size_t)w == data.len;
}

bool os_exists(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return GetFileAttributesA(cpath) != INVALID_FILE_ATTRIBUTES;
}

bool os_is_dir(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        DWORD a  = GetFileAttributesA(cpath);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool os_is_file(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        DWORD a  = GetFileAttributesA(cpath);
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool os_mkdir(Str path, uint32_t mode) {
        Unused(mode);
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return CreateDirectoryA(cpath, NULL) != 0;
}

bool os_mkdir_all(Arena* a, Str path, uint32_t mode) {
        char*  buf = str_to_cstr(a, path);
        size_t i;
        for (i = 1; buf[i]; i++) {
                if (buf[i] == '/' || buf[i] == '\\') {
                        buf[i] = '\0';
                        CreateDirectoryA(buf, NULL);
                        buf[i] = '/';
                }
        }
        return CreateDirectoryA(buf, NULL) ||
               GetLastError() == ERROR_ALREADY_EXISTS;
        Unused(mode);
}

bool os_remove(Str path) {
        char   cpath[4096];
        size_t l = path.len < 4095 ? path.len : 4095;
        memcpy(cpath, path.ptr, l);
        cpath[l] = '\0';
        return DeleteFileA(cpath) || RemoveDirectoryA(cpath);
}

bool os_rename(Str from, Str to) {
        char   cf[4096], ct[4096];
        size_t lf = from.len < 4095 ? from.len : 4095;
        size_t lt = to.len < 4095 ? to.len : 4095;
        memcpy(cf, from.ptr, lf);
        cf[lf] = '\0';
        memcpy(ct, to.ptr, lt);
        ct[lt] = '\0';
        return MoveFileExA(cf, ct, MOVEFILE_REPLACE_EXISTING) != 0;
}

Str os_readlink(Arena* a, Str path) {
        Unused(a);
        Unused(path);
        return str_null(); /* Windows symlinks need more work */
}

Slice(FileInfo) os_readdir(Arena* a, Str path) {
        char   pattern[4096];
        size_t l = path.len < 4093 ? path.len : 4093;
        memcpy(pattern, path.ptr, l);
        pattern[l]     = '/';
        pattern[l + 1] = '*';
        pattern[l + 2] = '\0';

        WIN32_FIND_DATAA fd;
        HANDLE           h = FindFirstFileA(pattern, &fd);

        Slice(FileInfo) entries = slice_make(a, FileInfo, 16);
        if (h == INVALID_HANDLE_VALUE) return entries;

        do {
                if (fd.cFileName[0] == '.' &&
                    (fd.cFileName[1] == '\0' ||
                     (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
                        continue;

                WIN32_FIND_DATAW fdw;
                memset(&fdw, 0, sizeof fdw);
                fdw.dwFileAttributes = fd.dwFileAttributes;
                fdw.ftLastWriteTime  = fd.ftLastWriteTime;
                fdw.ftLastAccessTime = fd.ftLastAccessTime;
                fdw.ftCreationTime   = fd.ftCreationTime;
                fdw.nFileSizeHigh    = fd.nFileSizeHigh;
                fdw.nFileSizeLow     = fd.nFileSizeLow;
                fdw.dwReserved0      = fd.dwReserved0;

                Str      name = str_clone(a, str_from_c(fd.cFileName));
                FileInfo fi   = _win_data_to_info(a, &fdw, name);
                slice_push(entries, FileInfo, fi);
        } while (FindNextFileA(h, &fd));

        FindClose(h);
        return entries;
}

Str os_getenv(Arena* a, Str key) {
        char* ckey = str_to_cstr(a, key);
        DWORD sz   = GetEnvironmentVariableA(ckey, NULL, 0);
        if (sz == 0) return str_null();
        char* buf = arena_push_array(a, char, (size_t)sz);
        GetEnvironmentVariableA(ckey, buf, sz);
        return str_buf(buf, (size_t)(sz - 1));
}

Str os_getcwd(Arena* a) {
        char* buf = arena_push_array(a, char, 4096);
        DWORD n   = GetCurrentDirectoryA(4096, buf);
        assert(n > 0 && "os_getcwd: failed");
        return str_buf(buf, (size_t)n);
}

#        endif /* platform */

#        include <glob.h>

Slice(Str) os_glob(Arena* a, Str pattern) {
        char*  cpat = str_to_cstr(a, pattern);
        glob_t g;
        Slice(Str) result = slice_make(a, Str, 8);

        if (glob(cpat, 0, NULL, &g) == 0) {
                size_t i;
                for (i = 0; i < g.gl_pathc; i++) {
                        Str path = str_clone(a, str_from_c(g.gl_pathv[i]));
                        slice_put(result, path);
                }
        }
        globfree(&g);
        return result;
}

#endif /* BAREOS_IMPLEMENTATION */
#endif /* BAREOS_H */
