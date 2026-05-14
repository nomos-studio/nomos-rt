/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Hand-written for kairos vendored liburcu — Linux x86_64 / aarch64. */

/* SMP support — always enabled; UP is a supported subset. */
#define CONFIG_RCU_SMP 1

/* TLS provided by the compiler. */
#define CONFIG_RCU_TLS __thread

/* clock_gettime() available. */
#define CONFIG_RCU_HAVE_CLOCK_GETTIME 1

/* Do not force sys_membarrier — urcu-bp selects it opportunistically. */
/* #undef CONFIG_RCU_FORCE_SYS_MEMBARRIER */

/* Debugging self-checks disabled in production builds. */
/* #undef CONFIG_RCU_DEBUG */

/* Use GCC/Clang atomic builtins (__atomic_*). */
#define CONFIG_RCU_USE_ATOMIC_BUILTINS 1

/* Do not emit legacy memory barriers — not needed on x86_64 / aarch64. */
/* #undef CONFIG_RCU_EMIT_LEGACY_MB */

/* Multi-flavor support (urcu-bp, urcu-mb, urcu-memb usable in same binary). */
#define CONFIG_RCU_HAVE_MULTIFLAVOR 1

/* Lock-free hash table iterator debugging disabled. */
/* #undef CONFIG_CDS_LFHT_ITER_DEBUG */
