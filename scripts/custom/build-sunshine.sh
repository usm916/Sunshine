#!/usr/bin/env bash
# Run through C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c.
set -euo pipefail
jobs="${1:-4}"
cmake_options=()
if [[ -x '/c/Program Files/Git/cmd/git.exe' ]]; then
  # Native Git avoids costly MSYS fork/status scans of the nested FFmpeg sources.
  cmake_options+=('-DGIT_EXECUTABLE=C:/Program Files/Git/cmd/git.exe')
fi
if [[ -f '/c/Program Files/nodejs/npm.cmd' ]]; then
  cmake_options+=('-DNPM=C:/Program Files/nodejs/npm.cmd')
fi
cmake -S . -B cmake-build-custom -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_TESTS=ON \
  -DBUILD_WERROR=OFF "${cmake_options[@]}"
cmake --build cmake-build-custom --parallel "$jobs"
(
  cd cmake-build-custom/tests
  ./test_sunshine.exe \
    --gtest_filter='ViewportCrop*:FocusAnchor*:WindowsFocusAnchor*:LowBandwidth*:VideoLowBandwidth*:AdaptiveBitrate*:NetworkMetrics*:Stream*:NvencBitrate*:NvencDynamicFactory*:VideoBitrate*:CursorFocus*:Clipboard*:WindowsClipboard*:InputPacketValidationTest*:ControlPacketTests*:ConcatAndInsertTests*:ConfigConsistency*:LocaleConsistency*:ConfigDefaults*:ConfigPersistence*' \
    --gtest_output=xml:../clipboard-tests.xml
)
gcc -std=c11 -Wall -Wextra -Werror \
  -Ithird-party/moonlight-common-c/src \
  third-party/moonlight-common-c/tests/clipboard.c \
  -o cmake-build-custom/clipboard-protocol-test.exe
./cmake-build-custom/clipboard-protocol-test.exe
cmake --install cmake-build-custom --strip --prefix "$PWD/cmake-build-custom/package"
cp LICENSE README.custom.md cmake-build-custom/package/
mkdir -p cmake-build-custom/package/docs/custom
cp docs/custom/*.md cmake-build-custom/package/docs/custom/
mkdir -p cmake-build-custom/package/scripts/custom
cp scripts/custom/*.ps1 scripts/custom/*.cs cmake-build-custom/package/scripts/custom/
cp "scripts/custom/Start Sunshine.cmd" cmake-build-custom/package/
./cmake-build-custom/package/sunshine.exe --help
