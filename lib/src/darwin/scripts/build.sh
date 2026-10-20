#!/usr/bin/env bash
# build.sh -- the Apogee macOS application's one build entry point.
#
#   build.sh build  [--target T] [--jobs N]   Apogee.app in Release, signed ad hoc
#   build.sh test   [--target T] [--jobs N]   the whole suite (the scheme's tests), hermetic
#   build.sh open   [--target T] [--jobs N]   build, then open the app
#   build.sh clean  [--target T]              remove build/<T>/
#
# The application is an Xcode project, Apogee.xcodeproj beside this directory,
# that owns its whole build -- the repo's one-application-one-build rule --
# and xcodebuild drives it through the shared scheme `Apogee`. lib/scripts/
# cicd.sh calls this script and knows nothing else about how the app
# compiles; CI calls cicd.sh. One build path, everywhere. Opening the project
# in Xcode builds the same targets with the same settings.
#
# The only target is macos-arm64 (macOS 27 or later on Apple silicon, the
# CLI's macOS target), built on such a Mac and nowhere else: cicd.sh never
# asks for it on another host, so a call from one is refused here too.
#
# Everything lands under build/<target>/, which the repository ignores:
#
#   build/<target>/xcode/       xcodebuild's derived data, both configurations
#   build/<target>/Apogee.app   the release build's bundle, its signature
#                               verified
#
# Signing is ad hoc (CODE_SIGN_IDENTITY "-" in the project): the user's call,
# 2026-10-20 -- no Developer ID and no notarization for now, so no signing
# secret exists anywhere. The cost, said plainly: Gatekeeper does not trust an
# ad-hoc signature, so on any Mac but the one that built it the first launch
# is right-click -> Open (or System Settings -> Privacy & Security -> Open
# Anyway). The version is stamped here, from the release's lib/release/VERSION;
# a build from Xcode carries none and says it is a development build.
#
# The suite needs no network, no `apogee` binary and no window: every test
# bundle is hosted by nothing, every test drives scripted fakes, and the
# view-models are SwiftUI-free.
set -euo pipefail

TARGETS=(macos-arm64)

log() { printf '[darwin] %s\n' "$*"; }
die() {
    printf '[darwin] error: %s\n' "$*" >&2
    [[ -n "${GITHUB_ACTIONS:-}" ]] && printf '::error title=darwin build.sh::%s\n' "$*"
    exit 1
}

usage() {
    sed -n '4,8p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

[[ $# -ge 1 ]] || usage
command="$1"
shift
case "$command" in
    build | test | open | clean) ;;
    -h | --help) usage ;;
    *) die "unknown command '${command}' (build, test, open, clean)" ;;
esac

target="macos-arm64"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            [[ $# -ge 2 ]] || die "--target needs a target"
            target="$2"
            shift
            ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs needs a number"
            jobs="$2"
            shift
            ;;
        -h | --help) usage ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

known=0
for t in "${TARGETS[@]}"; do [[ "$t" == "$target" ]] && known=1; done
[[ $known -eq 1 ]] || die "the macOS application builds only for ${TARGETS[*]} (asked for ${target})"

app_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${app_dir}/build/${target}"
derived="${build_dir}/xcode"
bundle="${build_dir}/Apogee.app"

if [[ "$command" == "clean" ]]; then
    log "cleaning ${build_dir}/"
    rm -rf "$build_dir"
    exit 0
fi

[[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] ||
    die "the macOS application builds on an Apple-silicon Mac only (this is $(uname -s) $(uname -m))"

# One line naming the toolchain before anything can fail on it: on a runner,
# the log is all there is.
log "toolchain: $(xcodebuild -version 2>/dev/null | tr '\n' ' ' || echo 'xcodebuild: none'); $(xcrun swift --version 2>/dev/null | head -n 1 || echo 'swift: none'); SDK $(xcrun --show-sdk-version 2>/dev/null || echo none)"

# The release this build belongs to, for the bundle's version: the repo's one
# release name (lib/release/VERSION), so the app reports what it ships in.
release_version() {
    local file="${app_dir}/../../release/VERSION" version
    version="$(tr -d ' \r\n' <"$file" 2>/dev/null)" || true
    [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "${file} must hold the release as x.y.z (got '${version}')"
    printf '%s' "$version"
}

# xcodebuild over the shared scheme, for this Mac's architecture, its derived
# data under build/<target>/. -quiet keeps the log to warnings, errors and
# the tests' own output.
xcodebuild_app() {
    xcodebuild -project "${app_dir}/Apogee.xcodeproj" -scheme Apogee \
        -destination "platform=macOS,arch=arm64" -derivedDataPath "$derived" \
        -jobs "$jobs" -quiet "$@"
}

build_app() {
    local version
    version="$(release_version)"
    log "[${target}] building Apogee.app ${version} (Release, ${jobs} jobs)"
    xcodebuild_app -configuration Release \
        MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="$version" build
    local built="${derived}/Build/Products/Release/Apogee.app"
    [[ -d "$built" ]] || die "xcodebuild reported success but built no ${built}"
    rm -rf "$bundle"
    ditto "$built" "$bundle"
    local said
    said="$(codesign --verify --deep --strict --verbose=1 "$bundle" 2>&1)" ||
        die "the signature does not verify: ${said}"
    log "[${target}] ${bundle}: Apogee ${version}, signed ad hoc, signature verified"
}

# Each failed test and its messages, from a result bundle.
report_failures() {
    xcrun xcresulttool get test-results tests --path "$1" 2>/dev/null | python3 -I -c '
import json, sys
def walk(node):
    if node.get("nodeType") == "Test Case" and node.get("result") == "Failed":
        print("[darwin] FAILED " + node.get("nodeIdentifier", "?"))
        def messages(child):
            if child.get("nodeType") == "Failure Message":
                print("    " + child.get("name", "").replace("\n", "\n    "))
            for grandchild in child.get("children", []):
                messages(grandchild)
        messages(node)
    for child in node.get("children", []):
        walk(child)
for node in json.load(sys.stdin).get("testNodes", []):
    walk(node)
' || log "the result bundle could not be read: ${1}"
}

case "$command" in
    build)
        build_app
        ;;
    open)
        build_app
        log "opening ${bundle}"
        open "$bundle"
        ;;
    test)
        log "[${target}] running the suite (Debug, ${jobs} jobs)"
        results="${build_dir}/tests.xcresult"
        rm -rf "$results"
        if ! xcodebuild_app -configuration Debug -resultBundlePath "$results" test; then
            # -quiet names a failed test but not why: the result bundle has
            # each failure's own message, and on a runner the log is all
            # there is.
            report_failures "$results"
            die "the suite failed (results: ${results})"
        fi
        log "[${target}] suite done"
        ;;
esac
