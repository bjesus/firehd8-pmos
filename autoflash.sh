#!/bin/bash
# Auto-flash TWRP the instant the hacked fastboot appears.
# Keep-alive probes keep the fragile LK USB stack busy (it dies when idle).
cd /home/bjesus/firehd8-pmos/amonet-dir/amonet
LOG=~/firehd8-pmos/autoflash.log
: > $LOG
echo "[$(date +%T)] armed - POWER-HOLD THE TABLET (each cycle retries)" | tee -a $LOG
cycle=0
while true; do
  cycle=$((cycle+1))
  # wait for device to appear
  while true; do
    if bin/fastboot devices 2>/dev/null | grep -q G090; then break; fi
    sleep 0.3
  done
  echo "[$(date +%T)] cycle $cycle: DEVICE SEEN - probing" | tee -a $LOG
  ok=0
  for i in $(seq 1 8); do
    if timeout 3 bin/fastboot getvar max-download-size 2>/dev/null | grep -q max-download-size; then
      ok=1
      echo "[$(date +%T)] probe OK (try $i)" | tee -a $LOG
      break
    fi
    echo "[$(date +%T)] probe fail (try $i)" | tee -a $LOG
    pkill -9 -x fastboot 2>/dev/null
    sleep 1
  done
  if [ $ok = 1 ]; then
    echo "[$(date +%T)] FLASHING TWRP NOW" | tee -a $LOG
    ./fastboot-step-v28.sh 2>&1 | tee -a $LOG
    echo "[$(date +%T)] flash script finished" | tee -a $LOG
    exit 0
  fi
  echo "[$(date +%T)] cycle $cycle: deaf - power-hold again to retry" | tee -a $LOG
  # wait for device to disappear (user power-holds) before re-arming
  while true; do
    if ! bin/fastboot devices 2>/dev/null | grep -q G090; then break; fi
    sleep 0.5
  done
done
