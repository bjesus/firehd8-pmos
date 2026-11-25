#!/bin/sh
# systemd-sleep hook for the Amazon Fire HD 8 (2017): the MT6625L consys stack
# (Wi-Fi + Bluetooth) does not survive s2idle, so power the chip off before sleep
# and back on afterwards; the musb gadget does not re-enumerate after resume, so
# re-pull it. Arguments: pre|post suspend.
case "$1" in
pre)
	rfkill block bluetooth 2>/dev/null
	nmcli radio wifi off 2>/dev/null
	sleep 1
	[ -c /dev/wmtWifi ] && echo 0 > /dev/wmtWifi
	sleep 1
	;;
post)
	[ -c /dev/wmtWifi ] && echo 1 > /dev/wmtWifi
	(
		i=0
		while [ ! -d /sys/class/net/wlan0 ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
		sleep 1
		nmcli radio wifi on 2>/dev/null
		rfkill unblock bluetooth 2>/dev/null
	) &
	(
		sleep 2
		for u in /sys/class/udc/*; do
			[ -e "$u/soft_connect" ] || continue
			echo disconnect > "$u/soft_connect"
			sleep 2
			echo connect > "$u/soft_connect"
		done
	) >/dev/null 2>&1 &
	;;
esac
exit 0
