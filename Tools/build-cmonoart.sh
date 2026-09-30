#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
video_lib="${BITLUNI_ESP32LIB:-$repo_root/vendor/bitluni_ESP32Lib_pal4x}"
output_dir="${1:-${TMPDIR:-/tmp}/Citadela-CMonoArt-build}"
staged_lib="${TMPDIR:-/tmp}/Citadela-bitluni-mono1-clean"

if [[ ! -f "$video_lib/src/Composite/CompositeColorDAC.cpp" ]] ||
   ! grep -q 'usesPAL4xEncoder' "$video_lib/src/Composite/CompositeColorDAC.h"; then
    echo "The Citadela PAL4x bitluni library is required." >&2
    exit 1
fi

# The Arduino install may contain Finder duplicate .cpp files that break linking.
mkdir -p "$staged_lib"
rsync -a --delete --exclude='* 2.cpp' "$video_lib/" "$staged_lib/"
if ! grep -q 'MODEPALMono1Super' "$staged_lib/src/Composite/CompMode.h" ||
   ! grep -q 'blitPAL4xMono1Picture' "$staged_lib/src/Composite/CompositeColorDAC.cpp"; then
    patch --quiet --directory "$staged_lib" --strip 1 \
        < "$repo_root/Tools/patches/bitluni-mono1.patch"
fi
if ! grep -q 'CITADELA_BITLUNI_MONO1' "$staged_lib/src/Composite/CompositeColorDAC.h"; then
    echo "The staged library does not contain the complete 1-bit extension." >&2
    exit 1
fi

arduino-cli compile --clean \
    --fqbn 'esp32:esp32:esp32:UploadSpeed=460800,CPUFreq=240,FlashFreq=80,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default' \
    --library "$staged_lib" \
    --output-dir "$output_dir" \
    "$repo_root/apps/AppCodes/CMonoArt"

cp "$output_dir/CMonoArt.ino.bin" "$repo_root/apps/CMonoArt.bin"
echo "CMonoArt image: $repo_root/apps/CMonoArt.bin"
