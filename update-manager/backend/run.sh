#!/bin/sh
set -e  # exit on any error
set -u  # treat unset variables as errors

echo "Starting broadcaster..."
/usr/local/bin/broadcaster &

BROADCASTER_PID=$!
echo "Broadcaster started with PID $BROADCASTER_PID"

# Wait a few seconds for broadcaster to initialize
sleep 2

# check if broadcaster is still running
if ! kill -0 $BROADCASTER_PID 2>/dev/null; then
    echo "ERROR: Broadcaster terminated unexpectedly"
    exit 1
fi

echo "Starting FastAPI server..."
exec uvicorn app.main:app --host 0.0.0.0 --port 8000
