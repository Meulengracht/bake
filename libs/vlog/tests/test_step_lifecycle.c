/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

int main(void)
{
    struct vlog_step a = { 0 };
    struct vlog_step b = { 0 };
    struct vlog_step unopened = { 0 };

    vlog_initialize(VLOG_LEVEL_DISABLED);

    if (vlog_step_open(NULL, "invalid") != -1) {
        fprintf(stderr, "expected NULL step_open to fail\n");
        goto fail;
    }
    if (vlog_step_open(&a, "first") != 0 || a.id == 0) {
        fprintf(stderr, "expected first step_open to succeed with a nonzero id\n");
        goto fail;
    }
    if (vlog_step_open(&b, "second") != 0 || b.id == 0) {
        fprintf(stderr, "expected second step_open to succeed with a nonzero id\n");
        goto fail;
    }
    if (b.id == a.id) {
        fprintf(stderr, "expected distinct step ids across step_open calls\n");
        goto fail;
    }

    if (vlog_step_update(NULL, VLOG_CONTENT_STATUS_WORKING, NULL) != -1) {
        fprintf(stderr, "expected NULL step_update to fail\n");
        goto fail;
    }
    if (vlog_step_update(&unopened, VLOG_CONTENT_STATUS_WORKING, NULL) != -1) {
        fprintf(stderr, "expected step_update on an unopened step to fail\n");
        goto fail;
    }
    if (vlog_step_update(&a, VLOG_CONTENT_STATUS_WORKING, "working\n") != 0) {
        fprintf(stderr, "expected step_update on an opened step to succeed\n");
        goto fail;
    }

    if (vlog_step_close(NULL, VLOG_CONTENT_STATUS_DONE, NULL) != -1) {
        fprintf(stderr, "expected NULL step_close to fail\n");
        goto fail;
    }
    if (vlog_step_close(&unopened, VLOG_CONTENT_STATUS_DONE, NULL) != -1) {
        fprintf(stderr, "expected step_close on an unopened step to fail\n");
        goto fail;
    }
    if (vlog_step_close(&a, VLOG_CONTENT_STATUS_DONE, "done\n") != 0 ||
        vlog_step_close(&b, VLOG_CONTENT_STATUS_FAILED, "failed\n") != 0) {
        fprintf(stderr, "expected step_close on opened steps to succeed\n");
        goto fail;
    }

    vlog_flush();
    vlog_cleanup();
    return 0;

fail:
    vlog_cleanup();
    return 1;
}
