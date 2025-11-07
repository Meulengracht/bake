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

#ifndef __SERVED_STATEMACHINE_H__
#define __SERVED_STATEMACHINE_H__

typedef unsigned int sm_event_t;
typedef unsigned int sm_state_t;

enum sm_action_result {
    // The statemachine can continue to run normally.
    SM_ACTION_CONTINUE,
    // The statemachine should abort and go into error state to disallow
    // continued execution of the current statemachine instance.
    SM_ACTION_ABORT,
    // The statemachine indicates that the statemachine has completed and
    // should be considerred done.
    SM_ACTION_DONE
};

typedef enum sm_action_result(*sm_action_t)(void*);

struct served_sm_transition {
    sm_event_t event;
    sm_state_t target_state;
};

struct served_sm_state {
    sm_state_t                  state;
    sm_action_t                 action;
    int                         transition_count;
    struct served_sm_transition transitions[];
};

struct served_sm_state_set {
    const struct served_sm_state** states;
    int                            states_count;
};

struct served_sm {
    struct served_sm_state_set states;
    sm_state_t                 state;
    void*                      context;
};

extern void served_sm_init(struct served_sm* sm, struct served_sm_state_set* stateSet, sm_state_t initialState, void* context);
extern void served_sm_destroy(struct served_sm* sm);

extern enum sm_action_result served_sm_execute(struct served_sm* sm);
extern void served_sm_event(struct served_sm* sm, sm_event_t event);

extern sm_state_t served_sm_current_state(struct served_sm* sm);

#endif //!__SERVED_STATEMACHINE_H__
