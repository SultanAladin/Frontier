#!/usr/bin/env bash
# Reference screenshots come from Chromium; native screenshots come from NativeValidation.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
images="$root/Exhibits/SunInspector/Images"
native="${1:-$root/_ClaudeScratch/build/sun/experimental-environment-evidence}"
for name in sun-overview sun-1 sun-2 sun-3 cloud-{0..3} atmosphere-{0..3} flare-{0..3}; do
    cp "$native/$name.png" "$images/native-$name.png"
done
for file in "$native"/scroll-*-end.png; do cp "$file" "$images/native-$(basename "$file")"; done
# The browser inspector begins at x=1060, y=34 in the 1440 x 900 reference capture.
while read -r label reference capture; do
    convert \( "$images/$reference" -crop 380x839+1060+34 +repage \) \
            \( "$native/$capture" -crop 380x839+0+0 +repage \) +append "$images/comparison-$label.png"
done <<'LIST'
clouds-overview reference-clouds-0.png cloud-0.png
atmosphere-overview reference-atmosphere-0.png atmosphere-0.png
flare-compose reference-flare-0.png flare-0.png
sun-daylight sun-daylight.png sun-1.png
LIST
# Browser scrollable viewport is 379x838; pad one pixel, and align the bottom of the
# 380x900 C++ host. Differing host chrome and content heights remain visible.
while read -r label reference capture; do
    convert \( "$images/ScrollReference/$reference" -background '#151515' -gravity west -extent 380x838 \) \
            \( "$native/$capture" -crop 380x838+0+62 +repage \) +append "$images/comparison-scroll-$label.png"
done <<'LIST'
sun-light sun-2-287.png scroll-sun-2-end.png
clouds-formation clouds-1-594.png scroll-cloud-1-end.png
atmosphere-medium atmosphere-1-672.png scroll-atmosphere-1-end.png
atmosphere-light atmosphere-2-465.png scroll-atmosphere-2-end.png
flare-ghosts flare-1-528.png scroll-flare-1-end.png
LIST
