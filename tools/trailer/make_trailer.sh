#!/bin/sh
# Builds the trailer from the real engine: records the gameplay shots offscreen (the bot plays,
# --god, fixed seeds: deterministic), synthesizes the music, cuts / treats / mixes everything and
# encodes it with GStreamer (WebM: VP9 + Opus). Needs python3 (numpy, scipy, Pillow) and
# gst-launch-1.0 with vp9enc / opusenc / webmmux. About 10 GB of raw frames in $TRAILER_DIR.
#
#   tools/trailer/make_trailer.sh [build/release/neon_breach]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${1:-$HERE/../../build/release/neon_breach}
export TRAILER_DIR=${TRAILER_DIR:-/tmp/trailer}
mkdir -p "$TRAILER_DIR/work"

# shot name, first frame, frames used (60 fps), options (see compose.py SEGS)
rec() {
    name=$1 from=$2 len=$3
    shift 3
    "$BIN" --record "$TRAILER_DIR/$name" --record-from $((from - 30)) --frames $((from + len + 30)) --god "$@"
}
rec A 1405 90 --seed 11 --weapon rockets --weapon cluster --upgrade fireworks --upgrade chain-reaction --enemy swarm --enemy kamikaze --enemy chaser --depth 25 &
rec B 495 90 --seed 13 --char knight --weapon arc --upgrade volatile-targets --enemy swarm --enemy leaper --enemy splitter --enemy chaser --depth 25 &
rec C 1615 60 --seed 12 --weapon laser --weapon prism --upgrade arc-emitter --enemy mirror --enemy serpent --enemy orbiter --enemy swarm --depth 25 &
rec D 1015 60 --seed 14 --weapon flamer --weapon wave --upgrade toxic-cloud --enemy splitter --enemy swarm --enemy guardian --depth 25 &
wait
rec E 2195 60 --seed 16 --weapon railgun --weapon graviton --upgrade black-hole --enemy lancer --enemy mortar --enemy shooter --enemy swarm --depth 25 &
rec F 888 60 --seed 18 --weapon scatter --weapon needler --upgrade shadow-clone --upgrade echo --active timestop --enemy wraith --enemy blinker --enemy ricochet --enemy swarm --depth 25 &
rec G 700 90 --seed 7 --boss --weapon seeker --weapon orbit --upgrade missile-pod --upgrade orbital-strike --active nova --active blades &
rec H 540 150 --seed 19 --all-upgrades --depth 25 &
wait

python3 "$HERE/music.py"
python3 "$HERE/compose.py" mix
python3 "$HERE/compose.py" render | gst-launch-1.0 -q fdsrc fd=0 blocksize=6220800 \
    ! rawvideoparse width=1920 height=1080 format=rgb framerate=30/1 ! videoconvert ! video/x-raw,format=I420 \
    ! vp9enc target-bitrate=16000000 cpu-used=4 deadline=1 threads=8 row-mt=true keyframe-max-dist=60 \
    ! queue max-size-time=0 max-size-buffers=0 max-size-bytes=0 ! webmmux name=mux \
    ! filesink location="$TRAILER_DIR/neon_breach_trailer.webm" \
    filesrc location="$TRAILER_DIR/work/mix.wav" ! wavparse ! audioconvert ! audioresample ! opusenc bitrate=192000 ! queue ! mux.
echo "trailer: $TRAILER_DIR/neon_breach_trailer.webm"
