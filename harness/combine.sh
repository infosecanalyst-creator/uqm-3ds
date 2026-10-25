#!/bin/sh
# combine.sh <snapdir> <name> -> <snapdir>/<name>.png laid out like the console
d=$1; n=$2
convert "$d/${n}_bottom.ppm" -bordercolor '#1b1b1b' -border 40x0 "$d/_b.ppm"
convert "$d/${n}_top.ppm" "$d/_b.ppm" -background '#1b1b1b' -append -bordercolor '#1b1b1b' -border 6 "$d/$n.png"
rm -f "$d/_b.ppm"
