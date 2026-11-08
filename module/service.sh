#!/system/bin/sh
# Start evdev mouse pass-through daemon (uinput)
sleep 15

MODDIR=/data/adb/modules/uu-wheel-evdev
CTRL=/data/local/tmp/uu_wheel_flip.invert
LOG=/data/local/tmp/uu_wheel_flip.log

# 0 = pass-through (default, verified correct with the v1.3+ stable virtual device), 1 = invert
if [ ! -f "$CTRL" ]; then
  echo 0 > "$CTRL"
fi
chmod 644 "$CTRL"

for p in $(pidof uu_wheel_flip); do
  kill -9 "$p" 2>/dev/null
done

chmod 755 "$MODDIR/uu_wheel_flip"

setsid "$MODDIR/uu_wheel_flip" >"$LOG" 2>&1 < /dev/null &
