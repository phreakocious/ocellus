"""PlatformIO pre-build hook: stamp the firmware with `git describe`.

Defines OCELLUS_VERSION for every env (see the global [env] section in platformio.ini). The command
is the same one flasher/publish.sh uses for the web-flasher manifest, so the esp-web-tools dialog
and the running unit finally report the same string.

--always is load-bearing: `git describe --tags` FAILS outright when no tag is reachable, and this
repo had no tags when the hook landed. Without it every build would fall back to "unknown" until
someone tagged a release, which is exactly the silent-wrong-answer this is meant to remove.

version.h carries a "dev" fallback for builds that never run this hook at all (no PlatformIO, no
git, a source tarball with no .git) -- protocol.cpp is in the Arduino-free native suite and has to
compile in all of those.
"""
import subprocess

Import("env")


def describe():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return "unknown"          # no git binary, no repo, or a tarball export
    return out.decode().strip() or "unknown"


env.Append(CPPDEFINES=[("OCELLUS_VERSION", env.StringifyMacro(describe()))])
