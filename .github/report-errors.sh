#!/usr/bin/env bash
# Show why a build failed. The full log shows the lines too, but only signed-in users can read
# it; an annotation is public. This puts the first error lines into one annotation.
log="$1"
cat "$log"
lines=$(grep -E -m 60 "error|Error|undefined reference|ld returned|ld: " "$log")
[ -n "$lines" ] || lines=$(tail -n 40 "$log")
# An annotation is one line: %, CR and LF must be encoded.
msg=$(printf '%s\n' "$lines" | sed -e 's/%/%25/g' | awk '{ printf "%s%%0A", $0 }')
echo "::error title=Build failed::${msg}"
