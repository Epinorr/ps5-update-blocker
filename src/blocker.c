/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "blocker.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef UB_HOST_TEST
#include <stdlib.h>
#endif

const char *const ub_paths[UB_PATH_COUNT] = {
    "PS5UPDATE.PUP", "PS5UPDATE.PUP.net.temp"
};
enum path_kind { PATH_ERROR = -1, PATH_ABSENT, PATH_OWNED, PATH_FOREIGN };

void ub_log(FILE *report, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
    if (report && report != stdout) {
        va_start(args, format);
        vfprintf(report, format, args);
        va_end(args);
        fputc('\n', report);
        fflush(report);
    }
}

const char *ub_state_name(enum ub_state state) {
    switch (state) {
    case UB_CLEAR: return "NO_OWNED_PLACEHOLDERS";
    case UB_PRESENT: return "PLACEHOLDERS_PRESENT";
    case UB_INCOMPLETE: return "PARTIAL_PLACEHOLDERS";
    case UB_FOREIGN: return "FOREIGN_CONTENT";
    default: return "UNKNOWN";
    }
}

static int fault(const char *point) {
#ifdef UB_HOST_TEST
    const char *wanted = getenv("UB_TEST_FAULT");
    if (wanted && !strcmp(wanted, point)) {
        errno = EIO;
        return 1;
    }
#else
    (void)point;
#endif
    return 0;
}

static void io_error(FILE *report, const char *operation, const char *name) {
    int saved = errno;
    ub_log(report, "ERROR operation=%s path=%s errno=%d (%s)",
           operation, name, saved, strerror(saved));
    errno = saved;
}

static int same_inode(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

/* An owned directory contains exactly one regular, singly linked marker
 * with the complete signature. Everything else is preserved as foreign. */
static enum path_kind inspect(int parent, const char *name, int *owned_fd,
                              FILE *report) {
    struct stat before, opened, marker, parent_info;
    int fd = -1, marker_fd = -1, scan_fd = -1;
    DIR *scan = NULL;
    enum path_kind kind = PATH_FOREIGN;
    char bytes[sizeof(UB_SIGNATURE)];
    size_t total = 0;
    *owned_fd = -1;
    if (fstatat(parent, name, &before, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT) return PATH_ABSENT;
        io_error(report, "fstatat", name);
        return PATH_ERROR;
    }
    if (fstat(parent, &parent_info)) {
        io_error(report, "fstat-parent", name);
        return PATH_ERROR;
    }
    if (!S_ISDIR(before.st_mode) || before.st_dev != parent_info.st_dev) return PATH_FOREIGN;
    fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        io_error(report, "open-directory", name);
        return PATH_ERROR;
    }
    if (fstat(fd, &opened)) goto fail;
    if (!same_inode(&before, &opened)) goto done;
    scan_fd = dup(fd);
    if (scan_fd < 0) goto fail;
    scan = fdopendir(scan_fd);
    if (!scan) goto fail;
    scan_fd = -1;
    int marker_count = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (!entry) {
            if (errno) goto fail;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strcmp(entry->d_name, UB_MARKER)) goto done;
        marker_count++;
    }
    if (marker_count != 1) goto done;
    if (fstatat(fd, UB_MARKER, &marker, AT_SYMLINK_NOFOLLOW)) goto fail;
    if (!S_ISREG(marker.st_mode) || marker.st_nlink != 1 ||
        marker.st_size != (off_t)(sizeof(UB_SIGNATURE) - 1)) goto done;
    marker_fd = openat(fd, UB_MARKER, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (marker_fd < 0) goto fail;
    if (fstat(marker_fd, &opened)) goto fail;
    if (!same_inode(&marker, &opened) || !S_ISREG(opened.st_mode) ||
        opened.st_nlink != 1 || opened.st_size != marker.st_size) goto done;
    while (total < sizeof(bytes)) {
        ssize_t count = read(marker_fd, bytes + total, sizeof(bytes) - total);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) goto fail;
        if (!count) break;
        total += (size_t)count;
    }
    if (total != sizeof(UB_SIGNATURE) - 1 ||
        memcmp(bytes, UB_SIGNATURE, sizeof(UB_SIGNATURE) - 1)) goto done;
    kind = PATH_OWNED;
    goto done;
fail:
    io_error(report, "inspect", name);
    kind = PATH_ERROR;
done:
    if (marker_fd >= 0) close(marker_fd);
    if (scan) closedir(scan);
    if (scan_fd >= 0) close(scan_fd);
    if (kind == PATH_OWNED) *owned_fd = fd;
    else close(fd);
    return kind;
}

static int sync_directory(int fd, struct ub_result *result, FILE *report) {
    if (!fault("directory-sync") && !fsync(fd)) return 0;
    if (errno == EINVAL || errno == ENOTSUP || errno == ENOSYS) {
        result->directory_sync_limited = 1;
        ub_log(report, "WARNING directory-fsync-unavailable; power-loss durability unverified");
        return 0;
    }
    io_error(report, "fsync-directory", "/update");
    return -1;
}

static int write_marker(int fd, FILE *report) {
    const char *signature = UB_SIGNATURE;
    size_t total = 0, length = sizeof(UB_SIGNATURE) - 1;
    int marker = openat(fd, UB_MARKER,
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (marker < 0) {
        io_error(report, "create-marker", UB_MARKER);
        return -1;
    }
    int error = 0;
    while (total < length) {
        ssize_t count = fault("marker-write") ? -1 :
                        write(marker, signature + total, length - total);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (!count) errno = EIO;
            io_error(report, "write-marker", UB_MARKER);
            error = -1;
            break;
        }
        total += (size_t)count;
    }
    if (!error && (fault("marker-sync") || fsync(marker))) {
        io_error(report, "fsync-marker", UB_MARKER);
        error = -1;
    }
    if (close(marker) && !error) {
        io_error(report, "close-marker", UB_MARKER);
        error = -1;
    }
    return error;
}

static int create_placeholder(int parent, int index, struct ub_result *result,
                              FILE *report) {
    const char *name = ub_paths[index];
    if (fault(index ? "mkdir-temp" : "mkdir-final") || mkdirat(parent, name, 0700)) {
        io_error(report, "mkdir", name);
        return -1;
    }
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        io_error(report, "open-new-directory", name);
        return -1;
    }
    int error = write_marker(fd, report);
    if (!error) error = sync_directory(fd, result, report);
    close(fd);
    if (!error) error = sync_directory(parent, result, report);
    if (!error) ub_log(report, "CREATED %s", name);
    /* On interruption, leave ambiguous content in place. Never erase it or
     * adopt it merely because its filename resembles one of our paths. */
    return error;
}

static int remove_placeholder(int parent, int index, struct ub_result *result,
                              FILE *report) {
    const char *name = ub_paths[index];
    int fd = -1;
    struct stat opened, named;
    if (inspect(parent, name, &fd, report) != PATH_OWNED) {
        ub_log(report, "ERROR ownership-changed-before-enable path=%s", name);
        return -1;
    }
    if (fstat(fd, &opened) || fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) ||
        !same_inode(&opened, &named)) {
        ub_log(report, "ERROR directory-identity-changed path=%s", name);
        close(fd);
        return -1;
    }
    if (fault(index ? "remove-temp" : "remove-final") || unlinkat(fd, UB_MARKER, 0)) {
        io_error(report, "unlink-owned-marker", name);
        close(fd);
        return -1;
    }
    int error = 0;
    if (fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) ||
        !same_inode(&opened, &named)) {
        ub_log(report, "ERROR directory-identity-changed-after-marker-removal path=%s", name);
        error = -1;
    } else if (unlinkat(parent, name, AT_REMOVEDIR)) {
        io_error(report, "rmdir-owned-placeholder", name);
        error = -1;
    }
    if (error) {
        /* A new foreign entry makes rmdir fail. Preserve that entry and try
         * to restore our marker through the original descriptor only. */
        if (write_marker(fd, report))
            ub_log(report, "WARNING marker-restore-failed; inspect this directory manually");
        (void)sync_directory(fd, result, report);
    }
    close(fd);
    if (sync_directory(parent, result, report)) error = -1;
    if (!error) ub_log(report, "REMOVED %s", name);
    return error;
}

static enum ub_state read_state(int parent, enum path_kind kinds[UB_PATH_COUNT],
                                FILE *report) {
    int owned = 0, absent = 0, foreign = 0, errors = 0;
    for (int i = 0; i < UB_PATH_COUNT; ++i) {
        int fd = -1;
        kinds[i] = inspect(parent, ub_paths[i], &fd, report);
        if (fd >= 0) close(fd);
        const char *text = "ERROR";
        switch (kinds[i]) {
        case PATH_OWNED: owned++; text = "OWNED_PLACEHOLDER"; break;
        case PATH_ABSENT: absent++; text = "ABSENT"; break;
        case PATH_FOREIGN: foreign++; text = "FOREIGN_PRESERVED"; break;
        default: errors++; break;
        }
        ub_log(report, "PATH %s=%s", ub_paths[i], text);
    }
    if (errors) return UB_UNKNOWN;
    if (foreign) return UB_FOREIGN;
    if (owned == UB_PATH_COUNT) return UB_PRESENT;
    if (absent == UB_PATH_COUNT) return UB_CLEAR;
    return UB_INCOMPLETE;
}

struct ub_result ub_run(int parent, enum ub_action action, FILE *report) {
    struct ub_result result = {UB_OK, UB_UNKNOWN, 0};
    enum path_kind kinds[UB_PATH_COUNT];
    int lock = -1;
    ub_log(report, "VERSION=%s ACTION=%s", UB_VERSION,
           action == UB_DISABLE ? "DISABLE" : action == UB_ENABLE ? "ENABLE" : "STATUS");
    ub_log(report, "HARDWARE_PERSISTENCE=UNVERIFIED");
    if (action != UB_STATUS && action != UB_DISABLE && action != UB_ENABLE) {
        result.code = UB_IO;
        goto done;
    }
    if (action != UB_STATUS) {
        struct stat info;
        lock = openat(parent, UB_LOCK,
                      O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        if (lock < 0) {
            io_error(report, "open-lock", UB_LOCK);
            result.code = UB_IO;
            goto done;
        }
        if (fstat(lock, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size) {
            ub_log(report, "ERROR foreign-lock-preserved");
            result.code = UB_CONFLICT;
            goto done;
        }
        if (flock(lock, LOCK_EX | LOCK_NB)) {
            result.code = (errno == EWOULDBLOCK || errno == EAGAIN) ? UB_BUSY : UB_IO;
            io_error(report, "lock", UB_LOCK);
            goto done;
        }
    }
    result.state = read_state(parent, kinds, report);
    if (result.state == UB_UNKNOWN) { result.code = UB_IO; goto done; }
    if (result.state == UB_FOREIGN) { result.code = UB_CONFLICT; goto done; }
    if (action == UB_STATUS) {
        if (result.state == UB_INCOMPLETE) result.code = UB_PARTIAL;
        goto done;
    }
    /* Both paths are checked before the first mutation. File PUPs, legacy
     * empty folders, symlinks, damaged markers and other contents are conflicts. */
    for (int i = 0; i < UB_PATH_COUNT; ++i) {
        if (action == UB_DISABLE && kinds[i] == PATH_ABSENT) {
            if (create_placeholder(parent, i, &result, report)) { result.code = UB_IO; break; }
        } else if (action == UB_ENABLE && kinds[i] == PATH_OWNED) {
            if (remove_placeholder(parent, i, &result, report)) { result.code = UB_IO; break; }
        }
    }
    if (action == UB_DISABLE && result.code == UB_OK) {
        /* Sync on repeat runs too; an earlier marker fsync may have failed. */
        for (int i = 0; i < UB_PATH_COUNT; ++i) {
            int fd = -1;
            if (inspect(parent, ub_paths[i], &fd, report) != PATH_OWNED) {
                result.code = UB_CONFLICT;
                break;
            }
            int marker = openat(fd, UB_MARKER, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
            if (marker < 0 || fault("marker-sync") || fsync(marker)) {
                io_error(report, "fsync-existing-marker", ub_paths[i]);
                result.code = UB_IO;
            }
            if (marker >= 0) close(marker);
            if (sync_directory(fd, &result, report)) result.code = UB_IO;
            close(fd);
            if (result.code != UB_OK) break;
        }
    }
    if (sync_directory(parent, &result, report)) result.code = UB_IO;
    result.state = read_state(parent, kinds, report);
    if (result.code == UB_OK) {
        enum ub_state expected = action == UB_DISABLE ? UB_PRESENT : UB_CLEAR;
        if (result.state != expected)
            result.code = result.state == UB_FOREIGN ? UB_CONFLICT : UB_PARTIAL;
    }
done:
    ub_log(report, "RESULT code=%d state=%s directory_sync_limited=%d",
           result.code, ub_state_name(result.state), result.directory_sync_limited);
    if (action == UB_DISABLE && result.code == UB_OK)
        ub_log(report, "Placeholders verified now. Reboot persistence and Sony updater behavior require hardware tests.");
    if (action == UB_ENABLE && result.code == UB_OK)
        ub_log(report, "Owned placeholders removed. Other blockers and system settings are unchanged.");
    if (lock >= 0) close(lock);
    return result;
}
