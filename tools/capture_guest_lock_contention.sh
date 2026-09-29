#!/system/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo 'usage: capture_guest_lock_contention.sh RENDER_THREAD_TID' >&2
  exit 2
fi
tid="$1"
case "$tid" in
  *[!0-9]*|'') echo 'render thread TID must be numeric' >&2; exit 2 ;;
esac

tracefs=/sys/kernel/tracing
begin="$tracefs/events/lock/contention_begin"
end="$tracefs/events/lock/contention_end"
[ "$(cat "$tracefs/tracing_on")" = 0 ] || { echo 'tracefs already active' >&2; exit 1; }
[ "$(cat "$begin/enable")" = 0 ] || { echo 'lock begin event already enabled' >&2; exit 1; }
[ "$(cat "$end/enable")" = 0 ] || { echo 'lock end event already enabled' >&2; exit 1; }
[ "$(cat "$begin/filter")" = none ] || { echo 'lock begin filter not empty' >&2; exit 1; }
[ "$(cat "$end/filter")" = none ] || { echo 'lock end filter not empty' >&2; exit 1; }
old_buffer="$(cat "$tracefs/buffer_size_kb")"

cleanup() {
  echo 0 > "$tracefs/tracing_on" || true
  echo 0 > "$begin/enable" || true
  echo 0 > "$end/enable" || true
  echo 0 > "$begin/filter" || true
  echo 0 > "$end/filter" || true
  echo "$old_buffer" > "$tracefs/buffer_size_kb" || true
}
trap cleanup EXIT HUP INT TERM

echo 2048 > "$tracefs/buffer_size_kb"
echo "common_pid == $tid" > "$begin/filter"
echo "common_pid == $tid" > "$end/filter"
echo > "$tracefs/trace"
echo 1 > "$begin/enable"
echo 1 > "$end/enable"
echo 1 > "$tracefs/tracing_on"
echo KIKI_LOCK_CAPTURE_START > "$tracefs/trace_marker"
input keyevent HOME
sleep 1
for cycle in 1 2 3 4; do
  input swipe 432 2 432 1100 450
  sleep 0.15
  input swipe 432 1100 432 2 450
  sleep 0.15
done
echo KIKI_LOCK_CAPTURE_END > "$tracefs/trace_marker"
echo 0 > "$tracefs/tracing_on"
cat "$tracefs/trace"
