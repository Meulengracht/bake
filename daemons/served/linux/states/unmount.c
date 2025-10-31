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

#include <transaction/states/unmount.h>
#include <transaction/transaction.h>
#include <state.h>

int served_application_unmount(struct served_application* application)
{
    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (application->mount != NULL) {
        __remove_application_symlinks(application);
        served_unmount(application->mount);
        application->mount = NULL;
    }
    return 0;
}

enum sm_action_result served_handle_state_unmount(void* context)
{
    struct served_transaction* transaction = context;

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
