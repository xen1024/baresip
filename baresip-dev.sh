#!/bin/bash

TARGET="./build/baresip"
#TARGET="baresip"
LAST_MOD=""

restart_app() {
    echo "[INFO] Restarting $TARGET..."
    pkill -f "$TARGET" 2>/dev/null
    "$TARGET" &
    echo "[INFO] $TARGET started with PID $!"
}

while true; do
    if [[ ! -f "$TARGET" ]]; then
        echo "[WARN] File not found: $TARGET"
        sleep 1
        continue
    fi

    CURRENT_MOD=$(stat -c %Y "$TARGET")

    if [[ "$CURRENT_MOD" != "$LAST_MOD" ]]; then
        LAST_MOD="$CURRENT_MOD"
        restart_app
    fi

    sleep 1
done
