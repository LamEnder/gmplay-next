#!/bin/bash
# Cross-compile gmplay's dependencies (FFmpeg, FBInk) as static libraries
# into deps/<platform>/, using the koxtoolchain + kindle-sdk sysroot.
#
#   usage: scripts/build-deps.sh [platform]     (default: kindlehf)
#
# Re-running is cheap: each dependency is skipped once its install stamp exists.
# Delete deps/<platform>/ (or the stamp file) to force a rebuild.
set -euo pipefail

PLATFORM="${1:-kindlehf}"
case "$PLATFORM" in
    kindlehf) TC_TARGET="arm-kindlehf-linux-gnueabihf"; FF_CPU="cortex-a9" ;;
    *) echo "[ERROR] unsupported platform: $PLATFORM" >&2; exit 1 ;;
esac

FFMPEG_VERSION="7.1.1"
FBINK_TAG="v1.25.0"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC_DIR="${TC_DIR:-$HOME/x-tools/$TC_TARGET}"
SYSROOT="$TC_DIR/$TC_TARGET/sysroot"
CROSS="$TC_DIR/bin/$TC_TARGET-"
SRC="$ROOT/deps/src"
PREFIX="$ROOT/deps/$PLATFORM"
BUILD="$ROOT/deps/build-$PLATFORM"
JOBS="$(nproc)"

[ -x "${CROSS}gcc" ] || { echo "[ERROR] toolchain not found at $TC_DIR" >&2; exit 1; }
mkdir -p "$SRC" "$PREFIX" "$BUILD"

# ---------------------------------------------------------------- FFmpeg ---
if [ ! -f "$PREFIX/.ffmpeg-$FFMPEG_VERSION" ]; then
    echo "[*] FFmpeg $FFMPEG_VERSION"
    tarball="$SRC/ffmpeg-$FFMPEG_VERSION.tar.xz"
    [ -f "$tarball" ] || curl -fL -o "$tarball" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
    rm -rf "$BUILD/ffmpeg" && mkdir -p "$BUILD/ffmpeg"
    tar -xf "$tarball" -C "$BUILD/ffmpeg" --strip-components=1

    # Containers and codecs people actually have lying around. Audio decoders
    # are included now so the audio step doesn't need an FFmpeg rebuild.
    demuxers="mov,matroska,avi,mpegts,mpegps,flv,ogg,wav,mp3,aac,flac,h264,hevc,m4v,ivf,asf"
    vdecoders="h264,hevc,mpeg4,h263,msmpeg4v1,msmpeg4v2,msmpeg4v3,mpeg1video,mpeg2video,vp8,vp9,mjpeg,theora,wmv1,wmv2,wmv3,vc1,flv"
    adecoders="aac,aac_latm,mp3,mp3float,mp2,opus,vorbis,flac,ac3,eac3,alac,wmav1,wmav2,pcm_s16le,pcm_s16be,pcm_s24le,pcm_u8,pcm_f32le"
    parsers="h264,hevc,mpeg4video,mpegvideo,vp8,vp9,vc1,mjpeg,aac,aac_latm,mpegaudio,opus,vorbis,flac,ac3"

    (cd "$BUILD/ffmpeg" && ./configure \
        --prefix="$PREFIX" \
        --enable-cross-compile --target-os=linux --arch=arm --cpu="$FF_CPU" \
        --cross-prefix="$CROSS" --sysroot="$SYSROOT" --pkg-config=false \
        --enable-static --disable-shared --enable-pic \
        --disable-autodetect --disable-debug --disable-doc --disable-programs \
        --disable-network --disable-avdevice --disable-avfilter \
        --disable-everything \
        --enable-zlib \
        --enable-protocol=file,pipe \
        --enable-demuxer="$demuxers" \
        --enable-decoder="$vdecoders,$adecoders" \
        --enable-parser="$parsers" \
        --enable-bsf=vp9_superframe_split \
        --enable-swscale --enable-swresample \
        --extra-cflags="-O2" \
        && make -j"$JOBS" && make install)
    touch "$PREFIX/.ffmpeg-$FFMPEG_VERSION"
fi

# ----------------------------------------------------------------- FBInk ---
# Handles the e-ink refresh ioctls for every Kindle generation (i.MX and MTK).
if [ ! -f "$PREFIX/.fbink-$FBINK_TAG" ]; then
    echo "[*] FBInk $FBINK_TAG"
    if [ ! -d "$SRC/FBInk" ]; then
        git clone --depth 1 --branch "$FBINK_TAG" --recurse-submodules --shallow-submodules \
            https://github.com/NiLuJe/FBInk.git "$SRC/FBInk"
    fi
    rm -rf "$BUILD/FBInk" && cp -a "$SRC/FBInk" "$BUILD/FBInk"
    # MINIMAL: no text/image rendering, just fb setup + refresh — that's all we need.
    make -C "$BUILD/FBInk" -j"$JOBS" staticlib \
        CROSS_TC="${CROSS%-}" KINDLE=true MINIMAL=true
    mkdir -p "$PREFIX/lib" "$PREFIX/include"
    cp "$BUILD/FBInk/Release/libfbink.a" "$PREFIX/lib/"
    cp "$BUILD/FBInk/fbink.h" "$PREFIX/include/"
    touch "$PREFIX/.fbink-$FBINK_TAG"
fi

echo "[*] Dependencies installed in $PREFIX"
