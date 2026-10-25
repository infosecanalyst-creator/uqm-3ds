#!/usr/bin/env bash
# Build The Ur-Quan Masters for the New 3DS, inside devkitPro's MSYS2:
#     bash /c/devkitPro/3ds-projects/uqm-3ds/build-uqm.sh
# First run downloads the UQM 0.8.0 source and game data from SourceForge
# (~150 MB, kept in downloads/). Every run re-applies the port's patches
# (idempotent), rebuilds, and leaves:
#     SD-CARD/3ds/uqm/uqm.3dsx                     (the only copy of the .3dsx)
#     SD-CARD/3ds/uqm/content/packages/*.uqm       (game data, sent once)
# Then on the PC:  bash /c/devkitPro/3ds-projects/install-3ds.sh <3DS IP> uqm
# (first time also: ... install-3ds.sh <3DS IP> --uqm-data)
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
HERE="$(cd "$(dirname "$0")" && pwd)"
SD="$HERE/../SD-CARD/3ds/uqm"
SF=https://downloads.sourceforge.net/project/sc2/UQM/0.8
cd "$HERE" || exit 1
LOG="$HERE/uqm-build.log"; : > "$LOG"
{
  echo "=== packages"
  need=""
  for p in 3ds-sdl 3ds-libvorbisidec 3ds-libogg 3ds-libpng 3ds-zlib; do
    pacman -Q $p >/dev/null 2>&1 || need="$need $p"
  done
  [ -n "$need" ] && pacman -S --needed --noconfirm $need

  echo "=== downloads"
  mkdir -p downloads
  for f in uqm-0.8.0-src.tgz uqm-0.8.0-content.uqm uqm-0.8.0-voice.uqm uqm-0.8.0-3domusic.uqm; do
    [ -s downloads/$f ] || curl -fL --retry 3 -o downloads/$f "$SF/$f" || { echo "download failed: $f"; exit 1; }
    ls -la downloads/$f
  done

  echo "=== source + 3DS patches"
  [ -d uqm-0.8.0 ] || tar xzf downloads/uqm-0.8.0-src.tgz
  python3 tools/patch_uqm.py uqm-0.8.0 || exit 1

  echo "=== build"
  make -f Makefile.3ds -j"$(nproc)" || { echo "BUILD FAILED"; exit 1; }
  mkdir -p "$SD/content/packages"
  mv -f uqm.3dsx "$SD/uqm.3dsx" && ls -la "$SD/uqm.3dsx"

  echo "=== game data"
  P="$SD/content/packages"
  if [ ! -s "$P/uqm-0.8.0-content.uqm" ]; then
    python3 tools/repack_stored.py downloads/uqm-0.8.0-content.uqm "$P/uqm-0.8.0-content.uqm" \
      || cp downloads/uqm-0.8.0-content.uqm "$P/"
  fi
  for f in uqm-0.8.0-voice.uqm uqm-0.8.0-3domusic.uqm; do
    [ -s "$P/$f" ] || cp downloads/$f "$P/"
  done
  printf '0.8.0\n' > "$SD/content/version"
  mkdir -p "$SD/content/addons"
  ls -la "$P"

  # Azahar (emulator) reads its own virtual SD card: give it the data too,
  # then open SD-CARD/3ds/uqm/uqm.3dsx in Azahar with File > Load File.
  AZ="$(cygpath -u "$APPDATA" 2>/dev/null)/Azahar/sdmc"
  if [ -d "$AZ" ]; then
    mkdir -p "$AZ/3ds/uqm/content/packages" "$AZ/3ds/uqm/content/addons"
    for f in "$P"/*.uqm; do
      [ -s "$AZ/3ds/uqm/content/packages/$(basename "$f")" ] || cp "$f" "$AZ/3ds/uqm/content/packages/"
    done
    cp -f "$SD/content/version" "$AZ/3ds/uqm/content/version"
    echo "game data mirrored to Azahar's SD card"
  fi
  echo "DONE"
} 2>&1 | tee -a "$LOG"
