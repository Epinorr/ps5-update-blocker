/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "blocker.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ps5/kernel.h>

#ifndef UB_ACTION
#error UB_ACTION must be selected at build time
#endif

struct notification_request {
    int32_t type, req_id, priority, msg_id, target_id, user_id;
    int32_t unk1, unk2, app_id, error_num, unk3;
    char use_icon_image_uri;
    char message[1024], uri[1024], unkstr[1024];
};
int sceKernelSendNotificationRequest(int device, struct notification_request *req,
                                     size_t size, int blocking);

static FILE *open_report(void) {
    const char *names[] = {"status.log", "disable.log", "enable.log"};
    if (mkdir("/data/ps5-update-blocker", 0700) && errno != EEXIST) return NULL;
    int dir = open("/data/ps5-update-blocker", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) return NULL;
    int fd = openat(dir, names[UB_ACTION],
                    O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
    close(dir);
    if (fd < 0) return NULL;
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || ftruncate(fd, 0)) {
        close(fd);
        return NULL;
    }
    FILE *file = fdopen(fd, "w");
    if (!file) close(fd);
    return file;
}

static void notify_result(struct ub_result result) {
    struct notification_request req;
    memset(&req, 0, sizeof(req));
    req.target_id = -1;
    const char *action = UB_ACTION == UB_DISABLE ? "Disable Updates" :
                         UB_ACTION == UB_ENABLE ? "Enable Updates" : "Status";
    if (result.code == UB_MOUNT)
        snprintf(req.message, sizeof(req.message),
                 "PS5 Update Blocker: /update is not a usable mount.\nReboot, jailbreak, then run before etaHEN/other unmounting HENs.");
    else if (result.code == UB_FIRMWARE)
        snprintf(req.message, sizeof(req.message),
                 "PS5 Update Blocker: firmware outside the 1.00-13.60 target.\nNo update paths changed.");
    else
        snprintf(req.message, sizeof(req.message),
                 "PS5 Update Blocker %s\n%s: %s (code %d)\nExperimental; persistence unverified. Log: /data/ps5-update-blocker/",
                 UB_VERSION, action, ub_state_name(result.state), result.code);
    (void)sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

int main(void) {
    FILE *report = open_report();
    struct ub_result result = {UB_IO, UB_UNKNOWN, 0};
    ub_log(report, "PS5 Update Blocker %s; console test build", UB_VERSION);
    if (!report) ub_log(report, "WARNING report-file-unavailable; using loader stdout only");
    unsigned int version = kernel_get_fw_version();
    ub_log(report, "FIRMWARE=0x%08x (%x.%02x) uid=%d euid=%d",
           version, version >> 24, (version >> 16) & 255, getuid(), geteuid());
    unsigned int family = version & 0xffff0000U;
    if (family < 0x01000000U || family > 0x13600000U) {
        result.code = UB_FIRMWARE;
        goto done;
    }
    int fd = open("/update", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        ub_log(report, "ERROR open-/update errno=%d (%s)", errno, strerror(errno));
        result.code = UB_MOUNT;
        goto done;
    }
    struct statfs mount;
    memset(&mount, 0, sizeof(mount));
    if (fstatfs(fd, &mount)) {
        ub_log(report, "ERROR fstatfs errno=%d (%s)", errno, strerror(errno));
        result.code = UB_MOUNT;
    } else {
        mount.f_mntonname[sizeof(mount.f_mntonname) - 1] = '\0';
        mount.f_mntfromname[sizeof(mount.f_mntfromname) - 1] = '\0';
        mount.f_fstypename[sizeof(mount.f_fstypename) - 1] = '\0';
        ub_log(report, "MOUNT on=%s from=%s type=%s flags=0x%llx",
               mount.f_mntonname, mount.f_mntfromname, mount.f_fstypename,
               (unsigned long long)mount.f_flags);
        if (strcmp(mount.f_mntonname, "/update") ||
            (UB_ACTION != UB_STATUS && (mount.f_flags & MNT_RDONLY))) {
            ub_log(report, "ERROR /update is not the mounted writable update filesystem; refusing mutation");
            result.code = UB_MOUNT;
        } else {
            result = ub_run(fd, (enum ub_action)UB_ACTION, report);
        }
    }
    close(fd);
done:
    ub_log(report, "EXIT=%d HARDWARE_PERSISTENCE=UNVERIFIED", result.code);
    if (report) {
        fflush(report);
        (void)fsync(fileno(report));
        fclose(report);
    }
    notify_result(result);
    return result.code;
}
