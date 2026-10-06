#!/usr/bin/env bash
source ./lib.sh

main() {
  log "starting"
  [ -n "$1" ] || die "no argument"
  grep -q x "$1"
}

main "$@"
