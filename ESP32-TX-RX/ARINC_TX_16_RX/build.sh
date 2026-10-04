#!/usr/bin/env bash
set -e

if ! command -v idf.py >/dev/null 2>&1; then
    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        source "$IDF_PATH/export.sh"
    else
        echo "ESP-IDF is not active. Source export.sh or set IDF_PATH." >&2
        exit 1
    fi
fi

if [ "$#" -eq 0 ]; then
    set -- build
fi
exec idf.py -C "$(dirname "$0")" "$@"
