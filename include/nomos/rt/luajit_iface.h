/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* C ABI contract for a loadable LuaJIT + Fennel participant.
 *
 * A shared library that exports nomos_rt_lua_init() can be loaded by
 * luajit_participant at startup.  The struct it returns owns the Lua state
 * and all associated resources; the caller never touches Lua directly.
 *
 * Thread safety: none — the caller serialises all calls to this interface
 * (typically via a std::mutex in luajit_participant).
 */
typedef struct nomos_rt_lua_iface {
    /* Optional periodic callback — called at ~25 Hz by the participant's
     * timer thread.  beat is the current Link beat position; bpm_hz is the
     * running tempo.  May be NULL.                                           */
    void (*tick)(double beat, float bpm_hz);

    /* Evaluate a Fennel expression.  src is a NUL-terminated string.
     * Returns a NUL-terminated EDN string that the caller must free via
     * free_result.  Returns NULL on catastrophic failure.                    */
    const char* (*eval)(const char* src);

    /* Free a string returned by eval.                                         */
    void (*free_result)(const char* result);
} nomos_rt_lua_iface_t;

/* Exported by any compatible Lua participant shared library.
 * Returns a pointer to the interface struct (program lifetime); never NULL
 * if the symbol exists — return NULL to signal initialisation failure.       */
nomos_rt_lua_iface_t* nomos_rt_lua_init(void);

#ifdef __cplusplus
}
#endif
