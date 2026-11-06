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

mkdir -p "$VIDEO_DIR"
# Detach so the library UI isn't left waiting on us.
"$GMPLAY_DIR/gmplay" $OPTS "$VIDEO_DIR" > "$GMPLAY_DIR/gmplay.log" 2>&1 &
