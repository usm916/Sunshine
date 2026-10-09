#!/usr/bin/env bash
# Run from the checkout through C:\msys64\msys2_shell.cmd -ucrt64 -c.
# First run pacman -Syu in MSYS2, close/reopen the terminal when requested,
# and repeat until no updates remain. Core-runtime updates need a new shell.
set -euo pipefail
pacman -S --needed --noconfirm git \
  mingw-w64-ucrt-x86_64-boost mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-cppwinrt mingw-w64-ucrt-x86_64-curl-winssl \
  mingw-w64-ucrt-x86_64-miniupnpc mingw-w64-ucrt-x86_64-onevpl \
  mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-opus \
  mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-qt6-static \
  mingw-w64-ucrt-x86_64-MinHook mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-nsis
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-libtiff mingw-w64-ucrt-x86_64-harfbuzz \
  mingw-w64-ucrt-x86_64-vulkan-headers
