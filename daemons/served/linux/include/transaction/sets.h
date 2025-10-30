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

#ifndef __SERVED_TRANSACTION_SETS_H__
#define __SERVED_TRANSACTION_SETS_H__

// common states
#include "states/completed.h"
#include "states/error.h"
#include "states/cancelled.h"

#include "states/prechecks.h"
#include "states/download.h"
#include "states/verify.h"
#include "states/dependencies.h"
#include "states/install.h"
#include "states/mount.h"
#include "states/load.h"
#include "states/start-services.h"
#include "states/generate-wrappers.h"

static const struct served_sm_state* g_stateSetInstall[] = {
    &g_statePreCheck,
    &g_stateDownload, &g_stateDownloadRetry,
    &g_stateVerify,
    &g_stateDependencies, &g_stateDependenciesWait,
    &g_stateInstall,
    &g_stateMount,
    &g_stateLoad,
    &g_stateStartServices,
    &g_stateGenerateWrappers,

    &g_stateCompleted, &g_stateError, &g_stateCancelled
};

#include "states/remove-wrappers.h"
#include "states/stop-services.h"
#include "states/unload.h"
#include "states/unmount.h"
#include "states/uninstall.h"

static const struct served_sm_state* g_stateSetUninstall[] = {
    &g_stateRemoveWrappers,
    &g_stateStopServices,
    &g_stateUnload,
    &g_stateUnmount,
    &g_stateUninstall,

    &g_stateCompleted, &g_stateError, &g_stateCancelled
};

#include "states/update.h"

static const struct served_sm_state* g_stateSetUpdate[] = {
    &g_statePreCheck,
    &g_stateDownload, &g_stateDownloadRetry,
    &g_stateVerify,
    &g_stateDependencies, &g_stateDependenciesWait,
    &g_stateRemoveWrappers,
    &g_stateStopServices,
    &g_stateUnload,
    &g_stateUnmount,
    &g_stateUpdate,
    &g_stateMount,
    &g_stateLoad,
    &g_stateStartServices,
    &g_stateGenerateWrappers,

    &g_stateCompleted, &g_stateError, &g_stateCancelled
};

#endif //!__SERVED_TRANSACTION_SETS_H__
