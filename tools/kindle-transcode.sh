#!/usr/bin/env bash
# Transcode videos into the format that's cheapest for gmplay to play on a
# Kindle: 12 fps, 360p, grayscale, H.264 tuned for fast decoding, AAC 44.1 kHz.
#
#   usage: tools/kindle-transcode.sh [options] FILE...
#
#   -f FPS     frame rate (default 12; sources that are already slower keep theirs)
#   -H PIXELS  height (default 360; smaller sources are not upscaled)
#   -c CODEC   h264 (default) or mpeg4 (lightest to decode, ~1/3 bigger files)
#   -q N       quality: x264 CRF for h264 (default 22), qscale for mpeg4
#              (default 4); lower = better + bigger
#   -n         no audio
#   -o DIR     output folder (default: ./kindle-videos)
#   -y         overwrite existing outputs (default: skip them)
#
# Why these defaults (measured on a Kindle PaperWhite 4, see README.md
# "Preparing videos"): the e-ink screen shows at most ~11 frames/s, so frames
# above 12 fps are decoded and thrown away; -tune fastdecode needs ~40% less
# CPU per frame; the Kindle is grayscale, so color only costs bits and time.
set -euo pipefail

fps=12 height=360 codec=h264 quality="" audio=1 outdir=kindle-videos overwrite=0

usage() { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while getopts "f:H:c:q:no:yh" opt; do
    case $opt in
        f) fps=$OPTARG ;;
        H) height=$OPTARG ;;
        c) codec=$OPTARG ;;
        q) quality=$OPTARG ;;
        n) audio=0 ;;
        o) outdir=$OPTARG ;;
        y) overwrite=1 ;;
        h) usage 0 ;;
        *) usage 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || usage 2

for tool in ffmpeg ffprobe; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found (install FFmpeg)" >&2; exit 1; }
done

case $codec in
    h264)  vopts=(-c:v libx264 -preset slow -crf "${quality:-22}" -tune fastdecode) ;;
    mpeg4) vopts=(-c:v mpeg4 -q:v "${quality:-4}") ;;
    *) echo "error: codec must be h264 or mpeg4" >&2; exit 2 ;;
esac

mkdir -p "$outdir"
status=0
for in in "$@"; do
    out="$outdir/$(basename "${in%.*}").mp4"
    if [ ! -f "$in" ]; then
        echo "error: not a file: $in" >&2
        status=1
        continue
    fi
    if [ -e "$out" ] && [ "$overwrite" = 0 ]; then
        echo "skip (exists): $out"
        continue
    fi
    # Audio options only when there is an audio stream to encode.
    aopts=(-an)
    if [ "$audio" = 1 ] && [ -n "$(ffprobe -v error -select_streams a:0 -show_entries stream=index \
                                     -of csv=p=0 "$in" || true)" ]; then
        aopts=(-map 0:a:0 -c:a aac -b:a 128k -ar 44100)
    fi

    # Keep the source frame rate if it's already at or below the target.
    src_fps=$( { ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate \
                 -of default=nw=1:nk=1 "$in" || true; } | awk -F/ '$2 > 0 { printf "%.3f", $1 / $2 }')
    vf=""
    if [ -z "$src_fps" ] || awk -v s="$src_fps" -v t="$fps" 'BEGIN { exit !(s > t + 0.01) }'; then
        vf="fps=$fps,"
    fi
    # Downscale only, to an even height; drop color but keep standard 8-bit
    # 4:2:0 (format=gray alone may be stored as full-range or 4:0:0, which
    # not every encoder handles the same way).
    vf+="scale=-2:trunc(min($height\,ih)/2)*2,format=gray,format=yuv420p"

    echo "==> $in -> $out"
    if ffmpeg -hide_banner -loglevel warning -stats -y -i "$in" -map 0:v:0 -vf "$vf" \
              "${vopts[@]}" "${aopts[@]}" "$out"; then
        ffprobe -v error -select_streams v:0 \
                -show_entries stream=codec_name,width,height,pix_fmt,avg_frame_rate \
                -of compact=p=0 "$out"
    else
        echo "error: failed: $in" >&2
        rm -f "$out"
        status=1
    fi
done
exit $status
