#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
video_lib="${BITLUNI_ESP32LIB:-$HOME/Documents/Arduino/libraries/bitluni_ESP32Lib}"
output_dir="${1:-${TMPDIR:-/tmp}/Citadela-bootloader-PAL4x-build}"
staged_lib="${TMPDIR:-/tmp}/Citadela-bitluni-PAL4x-clean"

if [[ ! -f "$video_lib/src/Composite/CompositeColorDAC.cpp" ]] ||
   ! grep -q 'usesPAL4xEncoder' "$video_lib/src/Composite/CompositeColorDAC.h" ||
   ! grep -q 'indexedRedLUT' "$video_lib/src/Graphics/ColorToBuffer/CTBComposite.h"; then
    echo "Citadela's PAL4x bitluni library is required. Set BITLUNI_ESP32LIB to its path." >&2
    exit 1
fi

# Some user installs contain duplicate Finder copies of .cpp files. Arduino
# compiles every .cpp in src, so stage a clean copy without changing the install.
mkdir -p "$staged_lib"
rsync -a --delete --exclude='* 2.cpp' "$video_lib/" "$staged_lib/"

arduino-cli compile \
    --fqbn 'esp32:esp32:esp32:UploadSpeed=460800,CPUFreq=240,FlashFreq=80,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default' \
    --library "$staged_lib" \
    --output-dir "$output_dir" \
    "$repo_root/System/bootloader"

cp "$output_dir/bootloader.ino.bin" "$repo_root/System/bootloader.bin"
cp "$output_dir/bootloader.ino.bin" "$repo_root/System/bootloader/bootloader.bin"
echo "PAL4x bootloader app image: $repo_root/System/bootloader.bin"
