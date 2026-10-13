#!/bin/bash
# stop a run cleanly: TERM the timeout wrapper of the given tag
TAG=$1
for p in $(pgrep -f "timeout -s TERM -k 5 [0-9]+ env LD_PRELOAD.*ts-c/view$TAG/game"); do echo "TERM $p"; kill -TERM $p; done
