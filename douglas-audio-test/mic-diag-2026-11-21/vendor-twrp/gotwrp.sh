sync
printf 'boot-recovery\0\0\0' | dd of=/dev/mmcblk0p12 bs=16 count=1 conv=fsync 2>/dev/null; sync
dd if=/dev/mmcblk0p12 bs=16 count=1 2>/dev/null | od -c | head -1
nohup sh -c 'sleep 2; reboot -f' >/dev/null 2>&1 &
echo armed
