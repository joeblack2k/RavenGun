#!/bin/bash
# usage: win-bridge.sh start <profile> | stop     -- the daemon's tp-shm input bridge, in the order scripts/play-halo.sh uses
export XDG_RUNTIME_DIR=/run/user/1000
case $1 in
  start) ravengunctl iobridge start "$2" --route tp-shm; ravengunctl arm; ravengunctl iobridge status; ls -la /dev/shm/ravengun_JvsState;;
  stop)  ravengunctl disarm; ravengunctl iobridge stop; ravengunctl iobridge status; ls -la /dev/shm/ravengun_JvsState 2>&1;;
esac
