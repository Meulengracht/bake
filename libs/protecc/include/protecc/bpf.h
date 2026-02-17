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

#ifndef __PROTECC_BPF_H__
#define __PROTECC_BPF_H__

#include <protecc/profile.h>

#include <bpf/bpf_helpers.h>

#define PROTECC_BPF_MAX_PATH         4096u
#define PROTECC_BPF_MAX_PROFILE_SIZE (65536u - 4u)

#define __VALID_PTR(base, max, ptr, size) \
    ((const void*)(ptr) >= (const void*)(base) && ((const void*)(ptr) + (size)) <= ((const void*)(base) + (max)))
#define __VALID_PROFILE_PTR(prof, ptr, size) \
    __VALID_PTR((prof), PROTECC_BPF_MAX_PROFILE_SIZE, (ptr), (size))
#define __VALID_PATH_PTR(path, ptr, size) \
    __VALID_PTR((path), PROTECC_BPF_MAX_PATH, (ptr), (size))

static __always_inline bool __profile_slice_in_bounds(__u32 offset, __u64 size, __u32 total) {
    __u64 end = (__u64)offset + size;
    if ((__u64)offset > (__u64)total) {
        return false;
    }
    if (end > (__u64)total) {
        return false;
    }
    return true;
}

static __always_inline bool __validate_profile_dfa(
    const __u8                      profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_header_t* header,
    const protecc_profile_dfa_t**   dfaOut,
    __u32*                          profileSizeOut,
    __u64*                          transitionsCountOut)
{
    const protecc_profile_dfa_t* dfa;
    __u32                        profileSize;
    __u64                        transitionsCount;
    __u64                        transitionsSize;
    __u64                        acceptSize;

    if (header == NULL || dfaOut == NULL || profileSizeOut == NULL || transitionsCountOut == NULL) {
        return false;
    }

    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return false;
    }

    if ((header->flags & PROTECC_PROFILE_FLAG_TYPE_DFA) == 0) {
        return false;
    }

    profileSize = header->stats.binary_size;
    if (profileSize < sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t) ||
        profileSize > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    dfa = (const protecc_profile_dfa_t*)(&profile[sizeof(protecc_profile_header_t)]);

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    if (dfa->start_state >= dfa->num_states) {
        return false;
    }

    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return false;
    }

    transitionsCount = (__u64)dfa->num_states * (__u64)dfa->num_classes;
    transitionsSize = transitionsCount * sizeof(__u32);
    acceptSize = (__u64)dfa->accept_words * sizeof(__u32);

    if (!__profile_slice_in_bounds(dfa->classmap_off, PROTECC_PROFILE_DFA_CLASSMAP_SIZE, profileSize)) {
        return false;
    }

    if ((dfa->accept_off & 3u) != 0u || !__profile_slice_in_bounds(dfa->accept_off, acceptSize, profileSize)) {
        return false;
    }

    if ((dfa->transitions_off & 3u) != 0u || !__profile_slice_in_bounds(dfa->transitions_off, transitionsSize, profileSize)) {
        return false;
    }

    *dfaOut = dfa;
    *profileSizeOut = profileSize;
    *transitionsCountOut = transitionsCount;
    return true;
}

static __always_inline bool __dfa_is_match(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u32                        state,
    const __u32*                 accept)
{
    __u32 wordIndex = state >> 5;
    __u32 bitIndex = state & 31u;

    if (wordIndex >= dfa->accept_words) {
        return false;
    }
    if (!__VALID_PROFILE_PTR(profile, &accept[wordIndex], sizeof(__u32))) {
        return false;
    }
    return (accept[wordIndex] & (1u << bitIndex)) != 0u;
}

static __always_inline bool protecc_bpf_match(
    const __u8 profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const __u8 path[PROTECC_BPF_MAX_PATH],
    __u32      pathStart,
    __u32      pathLength)
{
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)profile;
    const protecc_profile_dfa_t*    dfa;
    const __u8*                     classmap;
    const __u32*                    accept;
    const __u32*                    transitions;
    __u64                           transitionsCount;
    __u32                           profileSize;
    __u32                           state;
    __u32                           i;
    __u16                           iterCount;

    if (!__validate_profile_dfa(profile, header, &dfa, &profileSize, &transitionsCount)) {
        return false;
    }

    classmap = &profile[dfa->classmap_off];
    accept = (const __u32*)(&profile[dfa->accept_off]);
    transitions = (const __u32*)(&profile[dfa->transitions_off]);

    iterCount = PROTECC_BPF_MAX_PATH;
    if (pathLength < PROTECC_BPF_MAX_PATH) {
        iterCount = (__u16)pathLength;
    }

    // Ensure that classmap is within the bounds of the profile
    // to avoid verifier rejections when accessing it in the loop below.
    if (!__VALID_PROFILE_PTR(profile, classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE)) {
        return false;
    }

    state = dfa->start_state;
    bpf_for (i, 0, iterCount) {
        __u8  c = path[(pathStart + i) & (PROTECC_BPF_MAX_PATH - 1)];
        __u8  cls;
        __u64 transitionIndex;
        __u32 nextState;

        cls = classmap[c];
        if ((__u32)cls >= dfa->num_classes) {
            return false;
        }

        transitionIndex = ((__u64)state * (__u64)dfa->num_classes) + (__u64)cls;
        if (transitionIndex >= transitionsCount) {
            return false;
        }
        
        // ensure the transition pointer is within bounds of the profile before 
        // we access it, to avoid verifier rejections
        if (!__VALID_PROFILE_PTR(profile, &transitions[transitionIndex], sizeof(__u32))) {
            return false;
        }

        nextState = transitions[transitionIndex];
        if (nextState >= dfa->num_states) {
            return false;
        }
        state = nextState;
    }
    return __dfa_is_match(profile, dfa, state, accept);
}

#endif // !__PROTECC_BPF_H__
