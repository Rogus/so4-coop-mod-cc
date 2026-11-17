#!/bin/sh
# Read-only live check while the game runs: game audio streams (+ measured level) and thread states.
# Usage: live_check.sh OUTFILE   (waits up to 40 min for the game, then samples every 15 s until it exits)
OUT=$1
: > "$OUT"
log() { echo "$*" >> "$OUT"; }
gamepid() {
    for p in $(pgrep -f '[S]tarOceanTheLastHope\.exe'); do
        case "$(cat /proc/$p/comm 2>/dev/null)" in StarOcean*) echo "$p"; return;; esac
    done
}
end=$(( $(date +%s) + 2400 ))
OLD=$(gamepid)
[ "$ATTACH" = 1 ] && OLD=""                 # ATTACH=1: monitor the game that is already running
[ -n "$OLD" ] && log "waiting for the running game (pid $OLD) to close first"
while [ -n "$OLD" ] && [ -d /proc/$OLD ] && [ "$(date +%s)" -lt "$end" ]; do sleep 3; done
while [ -z "$(gamepid)" ] && [ "$(date +%s)" -lt "$end" ]; do sleep 3; done
PID=$(gamepid)
[ -z "$PID" ] && { log "game not started within 40 min"; exit 0; }
log "game pid $PID started $(date +%T)"
sleep 30
while [ -d /proc/$PID ]; do
    log "===== $(date +%T)  default sink: $(pactl get-default-sink)"
    # thread states: R running, S sleeping, D disk, T stopped, t traced (ptrace stop)
    st=$(for t in /proc/$PID/task/*; do awk '{print $3}' "$t/stat" 2>/dev/null; done | sort | uniq -c | tr '\n' ' ')
    log "threads: $st"
    for t in /proc/$PID/task/*; do
        s=$(awk '{print $3}' "$t/stat" 2>/dev/null)
        case "$s" in T|t) log "  STOPPED thread $(basename "$t") state=$s comm=$(cat "$t/comm" 2>/dev/null)";; esac
    done
    pactl list sink-inputs | awk -v RS='Sink Input #' 'NR>1 && /STAR OCEAN|StarOcean/ {print $1}' | while read -r idx; do
        info=$(pactl list sink-inputs | awk -v RS='Sink Input #' -v i="$idx" '$1==i' | grep -E 'Sink:|Corked|Mute:|^\s+Volume:|media.name' | sed 's/^\s*//' | tr '\n' ' ')
        level=$(timeout 3 parec --monitor-stream="$idx" --raw --format=s16le --channels=2 --rate=48000 2>/dev/null | python3 -I -c '
import sys, array, math
a = array.array("h"); a.frombytes(sys.stdin.buffer.read()[: (1 << 22) & ~1])
if not a: print("no samples"); sys.exit()
pk = max(abs(x) for x in a); rms = math.sqrt(sum(x * x for x in a) / len(a))
db = lambda v: "-inf" if v == 0 else "%.1f" % (20 * math.log10(v / 32768))
print("samples=%d peak=%s dBFS rms=%s dBFS" % (len(a), db(pk), db(rms)))')
        log "  stream #$idx $info| $level"
    done
    sink=$(pactl get-default-sink)
    log "  default sink state: $(pactl list short sinks | awk -v s="$sink" '$2==s {print $NF}')"
    sleep 15
done
log "game exited $(date +%T)"
