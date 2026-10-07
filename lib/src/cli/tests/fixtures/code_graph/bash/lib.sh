#!/usr/bin/env bash

log() {
  echo "[$(date +%T)] $*"
}

die() {
  log "fatal: $*"
  exit 1
}
