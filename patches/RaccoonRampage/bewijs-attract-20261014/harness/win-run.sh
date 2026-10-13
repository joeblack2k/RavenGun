#!/bin/bash
# usage: win-run.sh <profile> <share-folder-name> <exe-comm> <tag> <seconds> [K=V ...]
# Environment (exported by the caller): WIN_SHARE=ro|overlay, WIN_SHM_SOURCE=<file>, SHOTSECS=<s>,
#   WIN_EXPECT_CORE=ravengun|runtime  -- which core this run is DECLARED to use; the run is refused if the launcher's
#                                        plan says otherwise.
# The run's own record is <tag>.run: it names the core and the share mode this run used, and win-watch.sh appends,
# every 30 s, the full path of every core DLL mapped in the live game process.
PROF=$1; NASNAME=$2; COMM=$3; TAG=$4; SECS=$5
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/$NASNAME"
REC=$O/$TAG.run
START=$(date '+%Y-%m-%d %H:%M:%S')
{ echo "RUN $TAG -- $PROF -- started $START"
  echo "DECLARED: core=${WIN_EXPECT_CORE:-not declared}  share=${WIN_SHARE:-ro}  shm-source=${WIN_SHM_SOURCE:-launcher default}  limit=${SECS}s  captures every ${SHOTSECS:-10}s"
  echo "user layer ($HOME/.local/share/ravengun/patches/$PROF/game.ini) at start:"
  sed 's/^/    /' "$HOME/.local/share/ravengun/patches/$PROF/game.ini" 2>/dev/null || echo "    (none)"
} > $REC
PLAN=$(cd /home/deck/projects/ravengun && python3 scripts/launch-direct.py $PROF --plan 2>/dev/null | grep -E '^(core_dll|core_sha256|core_source|game|wine|prefix|profile_env):')
{ echo "launcher plan (scripts/launch-direct.py $PROF --plan):"; echo "$PLAN" | sed 's/^/    /'; } >> $REC
SRC=$(echo "$PLAN" | awk -F': ' '$1=="core_source"{print $2}')
if [ -n "$WIN_EXPECT_CORE" ] && [ "$SRC" != "$WIN_EXPECT_CORE" ]; then
  echo "REFUSED: the plan's core_source is '$SRC' but this run was declared as '$WIN_EXPECT_CORE' -- not started" | tee -a $REC; exit 2
fi
echo "$TAG start $(date +%H:%M:%S)" > $O/$TAG.txt
bash $O/win-watch.sh "$COMM" "$TAG" & WATCH=$!
/home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $O/win-inner.sh "$@" > $O/$TAG.outer 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S)" >> $O/$TAG.txt
wait $WATCH 2>/dev/null
{ echo "ENDED $(date '+%Y-%m-%d %H:%M:%S') -- launcher: $(grep -a 'klaar' $O/$TAG.out | tail -1 | sed 's/^\[launch-direct //; s/\]//') -- $(tail -1 $O/$TAG.out) (124 = stopped by the run's own limit)"
  echo "left running afterwards:"; bash $O/win-left.sh | sed 's/^/    /'
  echo "files in the share's game folder newer than the start of this run: $(find "$NAS" -newermt "$START" 2>/dev/null | wc -l)"
  echo "captures: $(ls $O/$TAG-shots 2>/dev/null | grep -c png), first $(ls $O/$TAG-shots | grep png | head -1), last $(ls $O/$TAG-shots | grep png | tail -1)"
} >> $REC
