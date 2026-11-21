#!/bin/bash
# One-shot amonet brick for Fire HD 8 (douglas)
# Usage: sudo ./brick-fresh.sh
# Run this IMMEDIATELY after the tablet enters fastboot mode.

set -e
cd /home/bjesus/firehd8-pmos/brick-dir/amonet

# Disable USB runtime suspend during the exploit (host-side only, harmless)
for d in /sys/bus/usb/devices/*/power/control; do
    echo on > "$d" 2>/dev/null || true
done

# Probe: bootloader must answer within 10 seconds
echo ">> Probing bootloader..."
if ! timeout 10 bin/fastboot getvar max-download-size; then
    echo ""
    echo "!! Bootloader is NOT answering (it wedges after sitting idle in fastboot)."
    echo "!! Power off the tablet (hold power 20-30s), re-enter fastboot"
    echo "!! (VolUp+Power -> recovery menu -> 'reboot to bootloader'),"
    echo "!! then run this script again IMMEDIATELY."
    exit 1
fi

echo ""
echo ">> Bootloader is alive - starting brick-9820"
echo ">> WATCH THE TABLET: 'USB Transferring...' should appear within ~30s."
echo ""
exec ./brick-9820.sh
