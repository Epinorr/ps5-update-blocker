/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "blocker.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[1], "--test-root")) return 64;
    enum ub_action action;
    if (!strcmp(argv[3], "status")) action = UB_STATUS;
    else if (!strcmp(argv[3], "disable")) action = UB_DISABLE;
    else if (!strcmp(argv[3], "enable")) action = UB_ENABLE;
    else return 64;
    int fd = open(argv[2], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return UB_MOUNT;
    struct ub_result result = ub_run(fd, action, stdout);
    close(fd);
    return result.code;
}
