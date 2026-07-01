#!/usr/bin/env bash
# Copyright 2026 chao.sun
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT="${HIIM_ROOT:-/workspace}"
cd "${ROOT}"

FORWARD_HOST="${HIIM_FORWARD_HOST:-hub}"
FORWARD_PORT="${HIIM_FORWARD_PORT:-28888}"
BACKEND_HOST="${HIIM_BACKEND_HOST:-hub}"
BACKEND_PORT="${HIIM_BACKEND_PORT:-28889}"
HEALTH_HOST="${HIIM_HEALTH_HOST:-hub}"
HEALTH_PORT="${HIIM_HEALTH_PORT:-8080}"
AUTH_USER="${HIIM_AUTH_USER:-proxy}"
AUTH_PASS="${HIIM_AUTH_PASS:-proxy}"

SMOKE_BIN="${ROOT}/build/hub_remote_smoke"

if [[ ! -x "${SMOKE_BIN}" ]]; then
  echo "[hiim-accept] binaries missing, building..."
  /workspace/deploy/docker/build-in-linux.sh bin
fi

wait_http() {
  local path="$1"
  local expect="$2"
  local url="http://${HEALTH_HOST}:${HEALTH_PORT}${path}"
  echo "[hiim-accept] waiting ${url} ..."
  for _ in $(seq 1 60); do
    if resp="$(curl -fsS "${url}" 2>/dev/null || true)"; then
      if [[ "${resp}" == *"${expect}"* ]]; then
        echo "[hiim-accept] ${path} OK"
        return 0
      fi
    fi
    sleep 1
  done
  echo "[hiim-accept] timeout waiting ${url}" >&2
  return 1
}

wait_tcp() {
  local host="$1"
  local port="$2"
  echo "[hiim-accept] waiting tcp ${host}:${port} ..."
  for _ in $(seq 1 60); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      echo "[hiim-accept] tcp ${host}:${port} OK"
      return 0
    fi
    sleep 1
  done
  echo "[hiim-accept] timeout waiting tcp ${host}:${port}" >&2
  return 1
}

wait_tcp "${FORWARD_HOST}" "${FORWARD_PORT}"
wait_tcp "${BACKEND_HOST}" "${BACKEND_PORT}"
wait_http "/healthz" "ok"
wait_http "/readyz" "ok"

export HIIM_FORWARD_HOST HIIM_FORWARD_PORT HIIM_BACKEND_HOST HIIM_BACKEND_PORT \
  HIIM_AUTH_USER HIIM_AUTH_PASS

echo "[hiim-accept] running hub_remote_smoke ..."
"${SMOKE_BIN}"

echo "[hiim-accept] all checks passed"
