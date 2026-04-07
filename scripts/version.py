"""
version.py — PlatformIO pre-build script
Injects git-derived build info as compiler defines at every build.

Defines injected:
  IWMP_VERSION        "1.0.0+NNN"          Full version with build number (overwrites platformio.ini)
  IWMP_BUILD_NUMBER   NNN                  Git commit count (monotonically increasing)
  IWMP_BUILD_HASH     "abc1234"            Short git SHA (7 chars)
  IWMP_BUILD_DIRTY    0 or 1               1 = uncommitted changes present

Usage in code:
  #include "version.h"
  LOG_I(TAG, "v%s (%s%s)", IWMP_VERSION, IWMP_BUILD_HASH, IWMP_BUILD_DIRTY ? "*" : "");
"""

import subprocess
Import("env")  # noqa: F821  — PlatformIO SCons global

# ---------------------------------------------------------------------------
# Base version — keep in sync with IWMP_VERSION_BASE in platformio.ini
# ---------------------------------------------------------------------------
BASE_VERSION = "1.0.0"


def _run(cmd, fallback):
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
        )
        return result.stdout.strip()
    except Exception:
        return fallback


build_number = _run(["git", "rev-list", "--count", "HEAD"], "0")
git_hash     = _run(["git", "rev-parse", "--short", "HEAD"], "unknown")
dirty_output = _run(["git", "status", "--porcelain"], "")
git_dirty    = "1" if dirty_output else "0"

full_version = f"{BASE_VERSION}+{build_number}"

# Append after any existing defines so these take precedence
env.Append(CPPDEFINES=[  # noqa: F821
    ("IWMP_VERSION",      env.StringifyMacro(full_version)),  # noqa: F821
    ("IWMP_BUILD_NUMBER", build_number),
    ("IWMP_BUILD_HASH",   env.StringifyMacro(git_hash)),      # noqa: F821
    ("IWMP_BUILD_DIRTY",  git_dirty),
])

dirty_marker = "*" if git_dirty == "1" else ""
print(f"[version] {full_version} ({git_hash}{dirty_marker})")
