#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
video_lib="${BITLUNI_ESP32LIB:-$HOME/Documents/Arduino/libraries/bitluni_ESP32Lib}"
output_dir="${1:-${TMPDIR:-/tmp}/Citadela-kernel-PAL4x-build}"

if [[ ! -f "$video_lib/src/Composite/CompositeColorDAC.cpp" ]] ||
   ! grep -q 'usesPAL4xEncoder' "$video_lib/src/Composite/CompositeColorDAC.h" ||
   ! grep -q 'indexedRedLUT' "$video_lib/src/Graphics/ColorToBuffer/CTBComposite.h"; then
    echo "Citadela's PAL4x bitluni library is required. Set BITLUNI_ESP32LIB to its path." >&2
    exit 1
fi

arduino-cli compile \
    --fqbn 'esp32:esp32:esp32:UploadSpeed=460800,CPUFreq=240,FlashFreq=80,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default' \
    --library "$video_lib" \
    --output-dir "$output_dir" \
    "$repo_root/System/kernel"

cp "$output_dir/kernel.ino.bin" "$repo_root/System/kernel.bin"
cp "$output_dir/kernel.ino.bin" "$repo_root/System/kernel/kernel.bin"
echo "PAL4x kernel image: $repo_root/System/kernel.bin"
