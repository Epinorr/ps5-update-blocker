/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5_UPDATE_BLOCKER_H
#define PS5_UPDATE_BLOCKER_H

#include <stdio.h>

#define UB_VERSION "0.1.0-experimental"
#define UB_MARKER ".ps5-update-blocker-v1"
#define UB_SIGNATURE "ps5-update-blocker\nformat=1\npolicy=nonempty-directory\n"
#define UB_LOCK ".ps5-update-blocker.lock"
#define UB_PATH_COUNT 2

enum ub_action { UB_STATUS = 0, UB_DISABLE = 1, UB_ENABLE = 2 };
enum ub_code {
    UB_OK = 0, UB_IO = 1, UB_CONFLICT = 2, UB_PARTIAL = 3,
    UB_MOUNT = 4, UB_BUSY = 5, UB_FIRMWARE = 6
};
enum ub_state { UB_UNKNOWN, UB_CLEAR, UB_PRESENT, UB_INCOMPLETE, UB_FOREIGN };
struct ub_result {
    enum ub_code code;
    enum ub_state state;
    int directory_sync_limited;
};

extern const char *const ub_paths[UB_PATH_COUNT];
void ub_log(FILE *report, const char *format, ...);
const char *ub_state_name(enum ub_state state);
/* update_fd must refer to the verified /update mount. This function never
 * follows a placeholder symlink, recursively deletes, downloads, or mounts. */
struct ub_result ub_run(int update_fd, enum ub_action action, FILE *report);

#endif
