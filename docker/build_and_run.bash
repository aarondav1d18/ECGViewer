#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="ecg-plotter"

cp Dockerfile ../

# Enable local X access temporarily
xhost +SI:localuser:root >/dev/null

# Build the image
sudo docker build -t "$IMAGE_NAME" ../

# Run container with X11 support
sudo docker run -it --rm \
  -e DISPLAY="${DISPLAY:?DISPLAY not set}" \
  -v /tmp/.X11-unix:/tmp/.X11-unix:ro \
  "$IMAGE_NAME"

# Revert permissions
xhost -SI:localuser:root >/dev/null
rm ../Dockerfile

