#!/usr/bin/env bash
# icon.sh -- the app's icon, made from the one master.
#
#   icon.sh generate        every copy, from lib/design/assets/Logo.png
#   icon.sh verify <app>    a built bundle names the icon and carries it
#
# lib/design/assets/Logo.png is the single source (GUI 11a). `generate` writes
# what is made from it:
#
#   source/main/Assets.xcassets/AppIcon.appiconset
#                           every macOS size, 16 to 1024 pixels, committed so
#                           a build runs no script
#   lib/design/assets/Logo-320.png
#                           the reduced copy the root README shows at 160
#   lib/design/spec/html/Logo.dc.html
#                           that copy, embedded in the design's logo board
#
# The images of 32 pixels or fewer are the rocket alone on black, its outline
# thickened (scripts/icon-small.swift): measured as macOS draws them, the whole
# artwork at those sizes is a gray smear, and so is a plain crop of it. Every
# other size is the whole artwork, as drawn.
#
# The set's Contents.json records the master's SHA-256 and each image's. The
# architecture tests' icon-set check reads them back, so a master changed
# without running this fails the suite, naming the stale set.
#
# `verify` is what build.sh asks of the bundle it built or is about to package.
# Neither of its callers regenerates anything.
set -euo pipefail

log() { printf '[icon] %s\n' "$*"; }
die() {
    printf '[icon] error: %s\n' "$*" >&2
    [[ -n "${GITHUB_ACTIONS:-}" ]] && printf '::error title=darwin icon.sh::%s\n' "$*"
    exit 1
}

usage() {
    sed -n '4,5p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

ICON=AppIcon

# The master's side, and the square around its rocket, in its pixels. A new
# master whose rocket moved or changed size needs this square measured again.
MASTER_SIDE=2048
ROCKET_SIDE=1700
ROCKET_TOP=166
ROCKET_LEFT=106
# How far the outline is thickened, in the master's pixels, at each small size.
THICKEN_16=40
THICKEN_32=22

app_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "${app_dir}/../../.." && pwd)"
master="${repo}/lib/design/assets/Logo.png"
readme_logo="${repo}/lib/design/assets/Logo-320.png"
catalog="${app_dir}/source/main/Assets.xcassets"
icon_set="${catalog}/${ICON}.appiconset"
board="${repo}/lib/design/spec/html/Logo.dc.html"

sha256() { shasum -a 256 <"$1" | cut -d ' ' -f 1; }

# resize <source> <pixels> <dpi> <out>: the source at that size. The density
# is what the asset catalog expects of a 1x (72) or 2x (144) image.
resize() {
    sips -s format png -s dpiWidth "$3" -s dpiHeight "$3" -z "$2" "$2" "$1" --out "$4" >/dev/null ||
        die "sips could not make ${4##*/} from ${1##*/}"
}

# The master has to be whole, and the square the crop was measured on: sips
# reads a PNG cut short from its header and fills in the rest, and stretches
# one of another shape, each without complaint.
check_master() {
    python3 -I - "$master" "$MASTER_SIDE" <<'PYTHON' || die "${master} cannot be used"
import struct, sys, zlib

path, side = sys.argv[1], int(sys.argv[2])
data = open(path, "rb").read()
if data[:8] != b"\x89PNG\r\n\x1a\n":
    sys.exit("[icon] error: the master is not a PNG")
at, size, ended = 8, None, False
while at + 12 <= len(data) and not ended:
    length, kind = struct.unpack(">I4s", data[at : at + 8])
    end = at + 12 + length
    if end > len(data):
        break
    if zlib.crc32(data[at + 4 : end - 4]) != struct.unpack(">I", data[end - 4 : end])[0]:
        sys.exit("[icon] error: the master is damaged: a chunk fails its checksum")
    if kind == b"IHDR":
        size = struct.unpack(">II", data[at + 8 : at + 16])
    ended = kind == b"IEND"
    at = end
if not ended:
    sys.exit("[icon] error: the master is cut short")
if size != (side, side):
    sys.exit(
        f"[icon] error: the master is {size[0]}x{size[1]} pixels: "
        f"the icon is square and the rocket's crop is measured on {side}x{side}, so measure it again"
    )
PYTHON
}

# The logo board with its one embedded copy replaced, written to <out>.
board_with_logo() {
    python3 -I - "$1" "$board" "$2" <<'PYTHON' || die "could not update ${board}"
import base64, re, sys

logo, board, out = sys.argv[1:]
text = open(board, encoding="utf-8", newline="").read()
data = base64.b64encode(open(logo, "rb").read()).decode()
line = "const LOGO = 'data:image/png;base64," + data + "';"
text, count = re.subn(r"^const LOGO = '.*';$", lambda _: line, text, flags=re.M)
if count != 1:
    sys.exit("[icon] error: the board has no single LOGO line")
open(out, "w", encoding="utf-8", newline="").write(text)
PYTHON
}

generate() {
    [[ -f "$master" ]] || die "no master at ${master}"
    check_master
    local set_stage small source radius images="" hashes="" points scale pixels name separator=""
    stage="$(mktemp -d "${TMPDIR:-/tmp}/apogee-icon.XXXXXX")"
    trap 'rm -rf "$stage"' EXIT
    set_stage="${stage}/${ICON}.appiconset"
    mkdir "$set_stage"
    small="${stage}/icon-small"
    xcrun swiftc -O -o "$small" "${app_dir}/scripts/icon-small.swift" ||
        die "could not compile scripts/icon-small.swift"

    for points in 16 32 128 256 512; do
        for scale in 1 2; do
            pixels=$((points * scale))
            name="icon_${points}x${points}.png"
            [[ $scale -eq 1 ]] || name="icon_${points}x${points}@${scale}x.png"
            source="$master"
            if [[ $pixels -le 32 ]]; then
                source="${stage}/rocket-${pixels}.png"
                radius="$THICKEN_32"
                [[ $pixels -gt 16 ]] || radius="$THICKEN_16"
                [[ -f "$source" ]] || "$small" "$master" "$source" "$pixels" \
                    "$ROCKET_SIDE" "$ROCKET_TOP" "$ROCKET_LEFT" "$radius" ||
                    die "could not make the ${pixels}-pixel rocket"
            fi
            resize "$source" "$pixels" $((72 * scale)) "${set_stage}/${name}"
            images+="${separator}    {
      \"filename\" : \"${name}\",
      \"idiom\" : \"mac\",
      \"scale\" : \"${scale}x\",
      \"size\" : \"${points}x${points}\"
    }"
            hashes+="${separator}      \"${name}\" : \"$(sha256 "${set_stage}/${name}")\""
            separator=$',\n'
        done
    done

    cat >"${set_stage}/Contents.json" <<JSON
{
  "images" : [
${images}
  ],
  "info" : {
    "author" : "xcode",
    "files-sha256" : {
${hashes}
    },
    "generated-by" : "lib/src/darwin/scripts/icon.sh",
    "master-sha256" : "$(sha256 "$master")",
    "version" : 1
  }
}
JSON
    resize "$master" 320 144 "${stage}/Logo-320.png"
    [[ ! -f "$board" ]] || board_with_logo "${stage}/Logo-320.png" "${stage}/Logo.dc.html"

    # Everything was made, so nothing is left half replaced.
    mkdir -p "$catalog"
    cat >"${catalog}/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON
    rm -rf "$icon_set"
    mv "$set_stage" "$icon_set"
    mv "${stage}/Logo-320.png" "$readme_logo"
    log "${icon_set#"${repo}/"}: ten sizes from ${master#"${repo}/"}"
    log "${readme_logo#"${repo}/"}: the README's copy"
    if [[ -f "${stage}/Logo.dc.html" ]]; then
        mv "${stage}/Logo.dc.html" "$board"
        log "${board#"${repo}/"}: its copy of the logo"
    else
        log "${board#"${repo}/"} is gone: no board to update"
    fi
}

verify() {
    local app="$1" plist="${1}/Contents/Info.plist" car="${1}/Contents/Resources/Assets.car" named said
    [[ -f "$plist" ]] || die "${app} has no Contents/Info.plist"
    named="$(plutil -extract CFBundleIconName raw "$plist" 2>/dev/null)" ||
        die "${app} names no icon: its Info.plist has no CFBundleIconName"
    [[ "$named" == "$ICON" ]] || die "${app} names the icon ${named}: it is ${ICON}"
    [[ -f "$car" ]] || die "${app} names ${ICON} but has no Contents/Resources/Assets.car"
    said="$(xcrun assetutil --info "$car" 2>/dev/null)" || die "${car} does not hold ${ICON}: it cannot be read"
    python3 -I -c '
import json, sys

icon = sys.argv[1]
wanted = {(16, 1), (32, 1), (32, 2), (64, 2), (128, 1), (256, 1), (256, 2), (512, 1), (512, 2), (1024, 2)}
entries = json.load(sys.stdin)
held = {
    (entry.get("PixelWidth"), entry.get("Scale"))
    for entry in entries
    if entry.get("Name") == icon and entry.get("AssetType") == "Icon Image"
}
sys.exit(0 if not wanted - held else 2 if held else 3)
' "$ICON" <<<"$said" || case $? in
        2) die "${car} does not hold ${ICON} at every size" ;;
        *) die "${car} does not hold ${ICON}" ;;
    esac
    log "${app##*/}: names ${ICON}, and its compiled catalog holds it at every size"
}

[[ $# -ge 1 ]] || usage
case "$1" in
    generate)
        [[ $# -eq 1 ]] || usage
        generate
        ;;
    verify)
        [[ $# -eq 2 ]] || usage
        verify "$2"
        ;;
    *) usage ;;
esac
