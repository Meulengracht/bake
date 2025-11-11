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

#include <transaction/sm.h>
#include <vlog.h>

#include <stddef.h>

void served_sm_init(struct served_sm* sm, struct served_sm_state_set* stateSet, sm_state_t initialState, void* context)
{
    sm->states = *stateSet;
    sm->state = initialState;
    sm->context = context;
    
    sm->event_queue.head = 0;
    sm->event_queue.tail = 0;
    sm->event_queue.count = 0;

    // queue first event to kickstart the state machine
    served_sm_post_event(sm, SM_EVENT_START);
}

void served_sm_destroy(struct served_sm* sm)
{
    // nothing to do currently
}

static sm_event_t __pop_event(struct served_sm* sm)
{
    sm_event_t event = sm->event_queue.events[sm->event_queue.head];
    sm->event_queue.head = (sm->event_queue.head + 1) % SM_EVENT_QUEUE_SIZE;
    sm->event_queue.count--;
    return event;
}

const struct served_sm_state* __get_current_state(struct served_sm* sm)
{
    for (int i = 0; i < sm->states.states_count; i++) {
        if (sm->states.states[i]->state == sm->state) {
            return sm->states.states[i];
        }
    }
    return NULL;
}

static sm_state_t __transition(const struct served_sm_state* state, sm_event_t event)
{
    for (int i = 0; i < state->transition_count; i++) {
        if (state->transitions[i].event == event) {
            return state->transitions[i].target_state;
        }
    }
    return state->state;
}

static int __process_event(struct served_sm* sm)
{
    const struct served_sm_state* currentState;
    sm_state_t                    nextState;
    sm_event_t                    event;

    if (sm->event_queue.count == 0) {
        return 0;
    }

    event = __pop_event(sm);
    if (event == SM_EVENT_START) {
        // This is supposed to kickstart the state machine without causing a transition
        // and acts as a no-op.
        VLOG_DEBUG("served", "__process_event: SM_EVENT_START received, no state transition\n");
        return 1;
    }

    VLOG_DEBUG("served", "__process_event: processing event %u (remaining: %d)\n", 
                event, sm->event_queue.count);
    
    currentState = __get_current_state(sm);
    if (currentState == NULL) {
        VLOG_ERROR("served", "__process_event: invalid current state: %u\n", sm->state);
        return 0;
    }

    nextState = __transition(currentState, event);
    if (nextState == currentState->state) {
        VLOG_WARNING("served", "__process_event: no transition for event %u in state %u\n", 
                    event, sm->state);
        return 0;
    }

    VLOG_DEBUG("served", "__process_event: transitioning from state %u to %u on event %u\n",
                sm->state, nextState, event);
    sm->state = nextState;
    return 1;
}

enum sm_action_result served_sm_execute(struct served_sm* sm)
{
    const struct served_sm_state* currentState;
    int                           transitioned;

    transitioned = __process_event(sm);
    if (transitioned == 0) {
        VLOG_DEBUG("served", "served_sm_execute: no events to process before executing state action\n");
        return SM_ACTION_CONTINUE;
    }

    currentState = __get_current_state(sm);
    if (currentState == NULL) {
        VLOG_ERROR("served", "served_sm_execute: invalid current state: %u\n", sm->state);
        return SM_ACTION_ABORT;
    }

    if (currentState->action != NULL) {
        return currentState->action(sm->context);
    }

    VLOG_ERROR("served", "served_sm_execute: no action defined for state: %u\n", sm->state);
    return SM_ACTION_CONTINUE;
}

void served_sm_post_event(struct served_sm* sm, sm_event_t event)
{
    // Check if queue is full
    if (sm->event_queue.count >= SM_EVENT_QUEUE_SIZE) {
        VLOG_ERROR("served", "served_sm_post_event: event queue full, dropping event %u\n", event);
        return;
    }

    // Add event to queue
    sm->event_queue.events[sm->event_queue.tail] = event;
    sm->event_queue.tail = (sm->event_queue.tail + 1) % SM_EVENT_QUEUE_SIZE;
    sm->event_queue.count++;
    
    VLOG_DEBUG("served", "served_sm_post_event: queued event %u (queue size: %d)\n", 
               event, sm->event_queue.count);
}

sm_state_t served_sm_current_state(struct served_sm* sm)
{
    return sm->state;
}
