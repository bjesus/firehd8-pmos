ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
$ADB kill-server >/dev/null 2>&1; $ADB start-server >/dev/null 2>&1
$ADB wait-for-recovery 2>&1 | head -1; $ADB devices
$ADB devices
$ADB shell 'echo shell-ok; id; cat /proc/version | cut -c1-60; cat /proc/asound/cards' 2>&1
cat > /tmp/twrp_inner.sh <<'IN'
L=$(grep mmcblk0p16 /proc/mounts | head -1 | cut -d' ' -f2); [ -z "$L" ] && { mkdir -p /mnt/lx; mount -o ro /dev/block/mmcblk0p16 /mnt/lx; L=/mnt/lx; }; echo "rootfs at $L"; grep mmcblk0p16 /proc/mounts; ls $L/lib/ld-musl-aarch64.so.1
export ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"
echo "== cards"; cat /proc/asound/cards; cat /proc/asound/pcm
echo "== controls (UL related)"; $A $L/usr/bin/amixer -c 0 controls 2>&1 | grep -i 'ADC\|Mic\|Preamp\|Debug\|SideGen\|PGA\|SineTable\|Loopback' 
echo "== enum items"; for c in 'Audio_MicSource1_Setting' 'Audio_MIC1_Mode_Select' 'Audio_Preamp1_Switch' 'Audio_ADC_1_Switch' 'Audio_PGA1_Setting' 'Audio_Debug_Setting' 'Audio_SideGen_Switch' 'SineTable_UL_ADC'; do $A $L/usr/bin/amixer -c 0 cget name="$c" 2>&1 | grep -v '^numid' | tr '\n' ' '; echo; done
echo "== set UL path"; 
$A $L/usr/bin/amixer -c 0 cset name='Audio_MicSource1_Setting' ADC1 2>&1 | tail -1
$A $L/usr/bin/amixer -c 0 cset name='Audio_MIC1_Mode_Select' ACCMODE 2>&1 | tail -1
$A $L/usr/bin/amixer -c 0 cset name='Audio_Preamp1_Switch' IN_ADC1 2>&1 | tail -1
$A $L/usr/bin/amixer -c 0 cset name='Audio_PGA1_Setting' 3 2>&1 | tail -1
$A $L/usr/bin/amixer -c 0 cset name='Audio_ADC_1_Switch' On 2>&1 | tail -1
CAPDEV=1
echo "capture dev index: '$CAPDEV'"; [ -z "$CAPDEV" ] && CAPDEV=$($A $L/usr/bin/arecord -l 2>&1 | grep -i 'device' | head -1 | sed 's/.*device \([0-9]*\):.*/\1/')
echo "using hw:0,$CAPDEV"
dmesg -c >/dev/null 2>&1
( sleep 1.5; $A $L/usr/bin/amixer -c 0 cset name='Audio_Debug_Setting' On >/dev/null 2>&1; sleep 0.3; $A $L/usr/bin/amixer -c 0 cset name='Audio_Debug_Setting' Off >/dev/null 2>&1 ) &
$A $L/usr/bin/arecord -D hw:0,$CAPDEV -f S16_LE -r 48000 -c 2 -d 4 /tmp/vendor_cap.wav 2>&1 | tail -2
ls -la /tmp/vendor_cap.wav
dmesg > /tmp/vendor_dmesg.txt 2>&1; wc -l /tmp/vendor_dmesg.txt
echo "== reg dump lines"; grep -i 'AUDIO_TOP_CON0\|AFE_DAC_CON0\|AFE_CONN3\|AFE_ADDA_UL_SRC_CON0\|AFE_VUL\|AFE_MEMIF\|TOP_CON3\|AFE_DAC_CON1' /tmp/vendor_dmesg.txt | head -20
IN
$ADB push /tmp/twrp_inner.sh /tmp/twrp_inner.sh >/dev/null 2>&1
$ADB shell 'sh /tmp/twrp_inner.sh' 2>&1
$ADB pull /tmp/vendor_cap.wav /root/douglas-flash/vendor_cap.wav 2>&1 | tail -1
$ADB pull /tmp/vendor_dmesg.txt /root/douglas-flash/vendor_dmesg.txt 2>&1 | tail -1
echo "== NOT rebooting yet"
