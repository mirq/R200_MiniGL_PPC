#!/bin/bash
# Wait until the physical Amiga's bridge port answers, then return.
# Usage: wait_bridge.sh [host] [port] [timeout_seconds]
HOST=${1:-192.168.1.21}
PORT=${2:-2345}
LIMIT=${3:-600}
i=0
while [ $i -lt $LIMIT ]; do
    if timeout 2 bash -c "exec 3<>/dev/tcp/$HOST/$PORT" 2>/dev/null; then
        # port open; give the bridge daemon a moment to settle
        sleep 1
        exit 0
    fi
    i=$((i + 2))
    sleep 2
done
echo "wait_bridge: timed out after ${LIMIT}s" >&2
exit 1
