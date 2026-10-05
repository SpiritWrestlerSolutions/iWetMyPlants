"""
version.py - PlatformIO pre-build script. Injects git-derived build info at every build.

  IWMP_VERSION        "2.0.0+NNN"   base version + git commit count
  IWMP_BUILD_HASH     "abc1234"     short git SHA
  IWMP_BUILD_DIRTY    0 or 1        1 = uncommitted changes present

BASE_VERSION below is the only place the version number lives. Bump it when cutting a release.
"""

import subprocess
Import("env")  # noqa: F821  - PlatformIO SCons global

BASE_VERSION = "2.0.0"


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
git_dirty    = "1" if _run(["git", "status", "--porcelain"], "") else "0"

full_version = f"{BASE_VERSION}+{build_number}"

env.Append(CPPDEFINES=[  # noqa: F821
    ("IWMP_VERSION",     env.StringifyMacro(full_version)),  # noqa: F821
    ("IWMP_BUILD_HASH",  env.StringifyMacro(git_hash)),      # noqa: F821
    ("IWMP_BUILD_DIRTY", git_dirty),
])

print(f"[version] {full_version} ({git_hash}{'*' if git_dirty == '1' else ''})")
