#!/bin/sh
if [ -f /var/lib/last-time ]; then
  SAVED=$(cat /var/lib/last-time)
  NOW=$(date +%s)
  if [ "$NOW" -lt "$SAVED" ]; then
    date -u -s "@$SAVED" >/dev/null 2>&1 || true
  fi
fi

i=0
while [ "$i" -lt 45 ]; do
  if ip link show wlan0 2>/dev/null | grep -q "LOWER_UP"; then
    udhcpc -i wlan0 -n -q >/dev/null 2>&1 || true
    if ntpd -n -q -p 216.239.35.0 >/dev/null 2>&1 || ntpd -n -q -p pool.ntp.org >/dev/null 2>&1; then
      date +%s > /var/lib/last-time
      sync
      break
    fi
  fi
  sleep 2
  i=$((i + 1))
done

while true; do
  sleep 300
  NOW=$(date +%s)
  if [ "$NOW" -gt 1700000000 ]; then
    echo "$NOW" > /var/lib/last-time
  fi
done
