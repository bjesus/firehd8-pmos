mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
R=/sys/kernel/debug/regmap/1000d000.pwrap/registers
dump() { grep -E '^(010[0-9a-f]|011[0-9a-f]|012[0-9a-f]|07[0-2][0-9a-f]|40[0-2][0-9a-f]):' $R | tr '\n' ' '; echo; }
echo "== idle"; dump
( sleep 1; echo "== during"; dump; echo "== cfg0 bits during"; grep -E '^(4024|4028|4000|4002|4012|4014|4016):' $R ) > /root/pmic.log 2>&1 &
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /root/cap.wav >/dev/null 2>&1
sleep 1; cat /root/pmic.log
echo "== top ckpdn"; grep -E '^(010[0-9a-f]|011[0-9a-f]):' $R | tr '\n' ' '; echo
