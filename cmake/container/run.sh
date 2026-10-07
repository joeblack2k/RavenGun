#!/bin/sh
# usage: build-in-container.sh <cmd...>; mount alleen cache en project
exec podman run --rm --userns=keep-id -v "$HOME/.cache/ravengun:$HOME/.cache/ravengun" -v "$HOME/projects/ravengun:$HOME/projects/ravengun" -v "$HOME/.local/share/ravengun/psmoveapi:$HOME/.local/share/ravengun/psmoveapi" -w "$PWD" -e HOME="$HOME" localhost/ravengun-build "$@"
