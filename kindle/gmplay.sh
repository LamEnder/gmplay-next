#!/bin/sh
# Name: gmplay
# Author: gmplay
#
# Scriptlet: copy this file to /mnt/us/documents/ on the Kindle; it shows up
# in the library. Opening it shows a browser of the videos in /mnt/us/videos/.
# The gmplay binary goes in /mnt/us/gmplay/.
#
# While playing, tap the screen for controls (seek, next/prev, close).
# gmplay pauses the Kindle UI / KOReader while it runs and restores the
# screen when it exits.

GMPLAY_DIR=/mnt/us/gmplay
VIDEO_DIR=/mnt/us/videos
OPTS=""   # e.g. "-g" grayscale, "-c 30" de-ghost every 30s, "-o" overdrive ("-O" max), "-f 8" fps cap,
          #      "-m" mute, "-a 250" audio delay in ms if sound and picture are out of sync

# kdc (per-pixel pulse drive, faster video): name of a preset in $KDC_DIR,
# e.g. "pw4_install1.doses" (fast) or "pw4_install1_long6.doses" (deeper
# blacks, slower). Empty = normal FBInk refresh. The pulse waveform is
# installed for the session and the stock waveform restored afterwards.
KDC_DIR=$GMPLAY_DIR/kdc
KDC_PRESET=""

mkdir -p "$VIDEO_DIR"

if [ -n "$KDC_PRESET" ] && [ -x "$KDC_DIR/kdc-pulse.sh" ] && [ -f "$KDC_DIR/$KDC_PRESET" ]; then
    # Detach so the library UI isn't left waiting on us.
    (
        # Keep the UI paused across install -> play -> restore: the waveform
        # reload must not race with UI refreshes.
        "$KDC_DIR/kdc-session.sh" pause
        sleep 1
        KOPT=""
        INSTALL=$(awk '$1 == "install" { print $2, $3; exit }' "$KDC_DIR/$KDC_PRESET")
        if "$KDC_DIR/kdc-pulse.sh" install $INSTALL; then
            KOPT="-K $KDC_DIR/$KDC_PRESET"
        else
            echo "gmplay.sh: kdc install failed, using FBInk"
        fi
        "$GMPLAY_DIR/gmplay" $OPTS $KOPT "$VIDEO_DIR"
        [ -n "$KOPT" ] && "$KDC_DIR/kdc-pulse.sh" restore
        "$KDC_DIR/kdc-session.sh" resume
    ) > "$GMPLAY_DIR/gmplay.log" 2>&1 &
else
    # Detach so the library UI isn't left waiting on us.
    "$GMPLAY_DIR/gmplay" $OPTS "$VIDEO_DIR" > "$GMPLAY_DIR/gmplay.log" 2>&1 &
fi
