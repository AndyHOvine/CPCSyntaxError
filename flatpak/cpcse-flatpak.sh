#!/bin/sh
# The Flatpak's start. The program's folder (/app) is read-only, so the program runs in a
# folder it can write: its settings, window layout, screenshots and ROM folder go there
# (~/.var/app/io.github.wackedman.CPCSyntaxError/data/cpcsyntaxerror). The shipped ROMs are
# copied into its ROM folder on the first start; a ROM file that is there already is kept.
home="${XDG_DATA_HOME:-$HOME/.local/share}/cpcsyntaxerror"
mkdir -p "$home/roms"
for f in /app/share/cpcsyntaxerror/roms/*; do
    [ -e "$home/roms/${f##*/}" ] || cp -r "$f" "$home/roms/"
done
cd "$home" || exit 1
export CPCSE_HOME="$home"
exec /app/bin/cpcse "$@"
