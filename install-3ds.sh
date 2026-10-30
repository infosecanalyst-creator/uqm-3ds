#!/usr/bin/env bash
# Send the build to a 3DS over Wi-Fi (FTP), no SD card swapping.
#
#   ./install-3ds.sh <3DS IP> uqm          # the game (uqm.3dsx), every update
#   ./install-3ds.sh <3DS IP> --uqm-data   # first time: ~150 MB of game data
#
# Start ftpd on the 3DS first; it shows the console's IP. The files come from
# the folder build-uqm.sh fills (../SD-CARD/3ds/uqm next to this repo); set
# UQM_SD to use another folder. FTP_PORT defaults to 5000 (ftpd's default).
set -euo pipefail

usage() { echo "usage: $0 <3DS IP> uqm|--uqm-data" >&2; exit 2; }
[ $# -eq 2 ] || usage
IP="$1"; WHAT="$2"
[[ "$IP" =~ ^[0-9A-Za-z.:-]+$ ]] || { echo "bad IP/host: $IP" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")" && pwd)"
SD="${UQM_SD:-$HERE/../SD-CARD/3ds/uqm}"
PORT="${FTP_PORT:-5000}"
URL="ftp://$IP:$PORT/3ds/uqm"

put() {   # put <local file> <remote path under 3ds/uqm/>
  [ -s "$1" ] || { echo "missing: $1 (run build-uqm.sh first)" >&2; exit 1; }
  echo "-> $2"
  curl -sS --fail --ftp-create-dirs --retry 2 -T "$1" "$URL/$2"
}

case "$WHAT" in
  uqm)
    put "$SD/uqm.3dsx" "uqm.3dsx"
    ;;
  --uqm-data)
    for f in "$SD"/content/packages/*.uqm; do
      put "$f" "content/packages/$(basename "$f")"
    done
    put "$SD/content/version" "content/version"
    # UQM expects content/addons to exist, even empty ('*' ignores "already exists").
    curl -sS --fail -Q "*MKD /3ds/uqm/content/addons" "$URL/content/" -o /dev/null
    ;;
  *) usage ;;
esac
echo "done"
