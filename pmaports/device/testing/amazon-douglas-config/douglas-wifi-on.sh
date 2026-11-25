#!/bin/sh
# Power the MT6625L Wi-Fi function on once the WMT launcher has answered the
# firmware-patch handshake, and wait for wlan0. NetworkManager takes it from there.
i=0
while [ ! -c /dev/wmtWifi ] && [ $i -lt 60 ]; do sleep 1; i=$((i + 1)); done
[ -c /dev/wmtWifi ] || { echo "douglas-wifi: /dev/wmtWifi never appeared"; exit 1; }
for attempt in 1 2 3; do
	echo 1 > /dev/wmtWifi 2>/dev/null
	w=0
	while [ ! -d /sys/class/net/wlan0 ] && [ $w -lt 20 ]; do sleep 1; w=$((w + 1)); done
	[ -d /sys/class/net/wlan0 ] && break
	sleep 2
done
[ -d /sys/class/net/wlan0 ] || { echo "douglas-wifi: wlan0 did not come up"; exit 1; }
ip link set wlan0 up 2>/dev/null || true
echo "douglas-wifi: wlan0 ready"
