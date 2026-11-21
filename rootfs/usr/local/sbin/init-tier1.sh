#!/bin/sh
insmod /root/mods/jsa1212.ko 2>/dev/null || true
# Resolve the sensor bus by its SoC address instead of hardcoding i2c-1:
# adding i2c@11008000 (CPU VPROC buck) to the DT shifts the bus numbering.
SENSOR_BUS=""
for d in /sys/bus/i2c/devices/i2c-*; do
  case "$(readlink -f $d/of_node 2>/dev/null)" in
    *i2c@11009000) SENSOR_BUS=$d ;;
  esac
done
[ -n "$SENSOR_BUS" ] || SENSOR_BUS=/sys/bus/i2c/devices/i2c-1
echo bma253 0x18 > $SENSOR_BUS/new_device 2>/dev/null || true
echo jsa1212 0x44 > $SENSOR_BUS/new_device 2>/dev/null || true
udevadm trigger --subsystem-match=iio --action=add 2>/dev/null || true
udevadm settle 2>/dev/null || true
pgrep -x iio-sensor-proxy >/dev/null || /usr/libexec/iio-sensor-proxy >/dev/null 2>&1 &
pgrep -x bluetoothd >/dev/null || /usr/lib/bluetooth/bluetoothd >/dev/null 2>&1 &
(
  # hci0 is registered by the STP bridge only once the WMT transport is ready
  # (a few seconds after boot); wait for it instead of racing it.
  i=0; while [ ! -d /sys/class/bluetooth/hci0 ] && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done
  sleep 2
  timeout 20 btmgmt --index 0 power on >/dev/null 2>&1 || true
  timeout 10 btmgmt --index 0 le on >/dev/null 2>&1 || true
  timeout 10 btmgmt --index 0 connectable on >/dev/null 2>&1 || true
  timeout 10 btmgmt --index 0 bondable on >/dev/null 2>&1 || true
  timeout 10 btmgmt --index 0 ssp on >/dev/null 2>&1 || true
  timeout 10 btmgmt --index 0 name "Fire HD 8" >/dev/null 2>&1 || true
) &
