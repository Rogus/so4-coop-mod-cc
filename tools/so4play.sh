#!/bin/bash
# Steam launch option to play co-op (real install):
#   "/path/to/so4-coop/tools/so4play.sh" %command%
# - loads so4coop (xinput1_3.dll) and Lyall's SO4Fix (Ultimate ASI Loader renamed to wininet.dll:
#   as winmm.dll, Proton loaded its builtin winmm and SO4Fix never ran). SO4Fix (2024) needs
#   msvcp140 >= 14.40; the prefix has 14.0 (crash at msvcp140+0x18c34), so msvcp140/vcruntime140(_1) 14.44
#   are in the game folder and are forced to native;
# - turns the keyboard into a virtual Xbox 360 controller (kbpad.py) while the game runs (player 4).
#   Physical controllers must be connected BEFORE the game starts. Steam numbers controllers in its own order,
#   so the virtual one may not be the last: use coop_join=1 or check so4coop.log.
DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$DIR/so4play.log"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}xinput1_3=n,b;wininet=n,b;msvcp140=n,b;vcruntime140=n,b;vcruntime140_1=n,b"
{ echo; echo "$(date -Is) so4play: $*"; } >> "$LOG"
# diagnostics: if the file tools/proton_log exists, Proton writes steam-609150.log to tools/
[ -e "$DIR/proton_log" ] && export PROTON_LOG=1 PROTON_LOG_DIR="$DIR"

if [ $# -eq 0 ]; then
    echo "usage (Steam launch option): $0 %command%" >&2
    exit 1
fi

KB=
if [ "${SO4_KBPAD:-1}" != 0 ]; then
    /usr/bin/python3 -I "$DIR/kbpad.py" >> "$LOG" 2>&1 &
    KB=$!
    sleep 1                                   # the virtual controller shows up in Steam before the game does
    kill -0 "$KB" 2>/dev/null || { echo "$(date -Is) so4play: kbpad did not start (see above); the game continues without the keyboard controller" >> "$LOG"; KB=; }
fi

"$@"
rc=$?
[ -n "$KB" ] && kill "$KB" 2>/dev/null && wait "$KB" 2>/dev/null
echo "$(date -Is) so4play: game closed (code $rc)" >> "$LOG"
exit $rc
