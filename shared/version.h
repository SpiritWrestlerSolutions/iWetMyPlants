/**
 * @file version.h
 * @brief Build version and identity macros
 *
 * IWMP_VERSION, IWMP_BUILD_NUMBER, IWMP_BUILD_HASH, and IWMP_BUILD_DIRTY are
 * all injected at compile time by scripts/version.py.  This header provides
 * safe fallbacks (for IDEs / static analysis) and convenience macros.
 *
 * Full version format:  "1.0.0+42"         (semantic + git commit count)
 * Build hash:           "abc1234"          (7-char short SHA)
 * Dirty flag:           0 = clean, 1 = has uncommitted changes
 *
 * To print the canonical build line at boot:
 *   LOG_I(TAG, "v" IWMP_VERSION " (" IWMP_BUILD_HASH IWMP_BUILD_DIRTY_MARKER ")");
 */

#pragma once

// ---------------------------------------------------------------------------
// Injected by scripts/version.py — fallbacks keep IDEs and CI happy
// ---------------------------------------------------------------------------

#ifndef IWMP_VERSION
#define IWMP_VERSION "1.0.0+0"
#endif

#ifndef IWMP_VERSION_BASE
#define IWMP_VERSION_BASE "1.0.0"
#endif

#ifndef IWMP_BUILD_NUMBER
#define IWMP_BUILD_NUMBER 0
#endif

#ifndef IWMP_BUILD_HASH
#define IWMP_BUILD_HASH "unknown"
#endif

#ifndef IWMP_BUILD_DIRTY
#define IWMP_BUILD_DIRTY 0
#endif

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------

/** Appended to build hash when working tree has uncommitted changes */
#if IWMP_BUILD_DIRTY
#define IWMP_BUILD_DIRTY_MARKER "*"
#else
#define IWMP_BUILD_DIRTY_MARKER ""
#endif

/**
 * Full human-readable build line, e.g.:
 *   "v1.0.0+42 (abc1234)"   — clean build
 *   "v1.0.0+42 (abc1234*)"  — dirty build
 *
 * Use as a string literal: LOG_I(TAG, IWMP_BUILD_LINE);
 */
#define IWMP_BUILD_LINE \
    "v" IWMP_VERSION " (" IWMP_BUILD_HASH IWMP_BUILD_DIRTY_MARKER ")"
