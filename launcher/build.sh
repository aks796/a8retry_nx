#!/bin/sh
# Build a8retry_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../a8retry_nx.nsp and ../a8retry_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=a8retry_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
