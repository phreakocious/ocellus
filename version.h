#pragma once

// Firmware version string. Normally injected by tools/version_stamp.py as
// `git describe --tags --always --dirty` -- the same command flasher/publish.sh stamps into the
// web-flasher manifest, so the flasher dialog and the running unit report the same thing.
//
// The fallback is not decoration: protocol.cpp consumes this and lives in the Arduino-free native
// suite, which must build with no git, no .git directory (a source tarball), and no PlatformIO to
// run the hook at all. "dev" is a visibly-not-a-release answer for exactly those builds.
#ifndef OCELLUS_VERSION
#define OCELLUS_VERSION "dev"
#endif
