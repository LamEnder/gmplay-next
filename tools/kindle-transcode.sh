#!/usr/bin/env bash
# Transcode videos into the format that's cheapest for gmplay to play on a
# Kindle: 12 fps, 360p, grayscale, H.264 tuned for fast decoding. The audio is
# kept as it is whenever gmplay can play it, so it loses no quality.
#
#   usage: tools/kindle-transcode.sh [options] FILE...
#
#   -f FPS     frame rate (default 12; sources that are already slower keep theirs)
#   -H PIXELS  height (default 360; smaller sources are not upscaled)
#   -c CODEC   h264 (default) or mpeg4 (lightest to decode, ~1/3 bigger files)
#   -q N       quality: x264 CRF for h264 (default 22), qscale for mpeg4
#              (default 4); lower = better + bigger
#   -a MODE    audio: auto (default) copies AAC/MP3 mono or stereo audio
#              unchanged and re-encodes anything else; copy always copies;
#              aac always re-encodes
#   -b RATE    AAC bitrate when re-encoding (default 256k)
#   -n         no audio
#   -o DIR     output folder (default: ./kindle-videos)
#   -y         overwrite existing outputs (default: skip them)
#
# Why these defaults (measured on a Kindle PaperWhite 4, see README.md
# "Preparing videos"): the e-ink screen shows at most ~11 frames/s, so frames
# above 12 fps are decoded and thrown away; -tune fastdecode needs ~40% less
# CPU per frame; the Kindle is grayscale, so color only costs bits and time.
# Audio: decoding and re-encoding already-compressed audio adds a second round
# of artefacts (signal-to-distortion vs. the source: 16 dB at 128k, 26 dB at
# 256k, lossless when copied), while the Kindle's cost to play the original is
# small. Re-encodes use the soxr resampler when FFmpeg has it, mix surround down
# without overshooting (rematrix_maxval), and lower the volume just enough to
# leave 1.5 dB of headroom if the result would clip: hot 5.1 tracks otherwise
# peak several dB over full scale once downmixed.
set -euo pipefail

fps=12 height=360 codec=h264 quality="" audio=auto abitrate=256k outdir=kindle-videos overwrite=0

# Prints the header comment, up to the "Why these defaults" notes.
usage() { awk 'NR > 1 && /^# Why/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"; exit "${1:-0}"; }

while getopts "f:H:c:q:a:b:no:yh" opt; do
    case $opt in
        f) fps=$OPTARG ;;
        H) height=$OPTARG ;;
        c) codec=$OPTARG ;;
        q) quality=$OPTARG ;;
        a) audio=$OPTARG ;;
        b) abitrate=$OPTARG ;;
        n) audio=none ;;
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
case $audio in
    auto|copy|aac|none) ;;
    *) echo "error: audio mode must be auto, copy or aac" >&2; exit 2 ;;
esac
# Re-encode chain: resample to 44.1 kHz (soxr if this FFmpeg has it) and keep
# downmixes from overshooting full scale; float so peaks above 0 dB survive to
# be measured.
resample="aresample=44100:rematrix_maxval=1.0"
if ffmpeg -hide_banner -buildconf 2>/dev/null | grep -q -- --enable-libsoxr; then
    resample="aresample=44100:resampler=soxr:precision=28:rematrix_maxval=1.0"
fi

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
    # Audio: copy when gmplay can play it from an MP4 as is, else re-encode.
    aopts=(-an) anote="none"
    if [ "$audio" != none ]; then
        # key=value output: ffprobe prints fields in its own order, not ours
        ainfo=$(ffprobe -v error -select_streams a:0 \
                -show_entries stream=codec_name,channels,sample_rate -of default=nw=1 "$in" || true)
        acodec=$(sed -n 's/^codec_name=//p' <<< "$ainfo")
        achans=$(sed -n 's/^channels=//p' <<< "$ainfo")
        arate=$(sed -n 's/^sample_rate=//p' <<< "$ainfo")
        if [ -n "$acodec" ]; then
            if [ "$audio" = copy ] || { [ "$audio" = auto ] &&
                                        { [ "$acodec" = aac ] || [ "$acodec" = mp3 ]; } &&
                                        [ "${achans:-0}" -le 2 ]; }; then
                aopts=(-map 0:a:0 -c:a copy)
                anote="copied ($acodec, $achans ch, $arate Hz)"
            else
                af="$resample,aformat=sample_fmts=flt"
                [ "${achans:-2}" -gt 2 ] && af+=":channel_layouts=stereo"   # downmix
                # Measure the peak of exactly what will be encoded; if it would
                # clip, lower the volume to leave 1.5 dB headroom (plain gain,
                # no limiter: the sound is otherwise unchanged).
                peak=$( { ffmpeg -hide_banner -nostats -i "$in" -map 0:a:0 -vn \
                          -af "$af,astats=measure_perchannel=none:measure_overall=Peak_level" \
                          -f null - 2>&1 || true; } | sed -n 's/.*Peak level dB: \(-\{0,1\}[0-9.]*\).*/\1/p' | tail -1)
                gain=$(awk -v p="${peak:--99}" 'BEGIN { if (p > -1.5) printf "%.2f", -1.5 - p }')
                [ -n "$gain" ] && af+=",volume=${gain}dB"
                aopts=(-map 0:a:0 -af "$af" -c:a aac -b:a "$abitrate")
                anote="re-encoded ($acodec, $achans ch, $arate Hz -> AAC $abitrate, 44100 Hz"
                [ "${achans:-2}" -gt 2 ] && anote+=", downmixed to stereo"
                [ -n "$gain" ] && anote+=", peak ${peak} dB -> gain ${gain} dB"
                anote+=")"
            fi
        fi
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
    echo "    audio: $anote"
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
