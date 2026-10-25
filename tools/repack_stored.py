#!/usr/bin/env python3
"""Rewrite a .uqm (zip) package with every entry stored uncompressed.
Usage: repack_stored.py <in.uqm> <out.uqm>

UQM reads its graphics, fonts and scripts out of this zip on every screen
change; on the 3DS's 804 MHz ARM11 inflating ~10,000 deflated entries is a
visible part of load times. Stored entries are read straight off the SD
card. Costs ~3.6 MB more space for the base content pack; the music and
voice packs are already stored (Ogg is incompressible) and are left alone."""
import sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as zo:
    for info in zi.infolist():
        data = zi.read(info)
        out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
        out.external_attr = info.external_attr
        out.compress_type = zipfile.ZIP_STORED
        zo.writestr(out, data)
print("repacked", dst)
