ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner2.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf ALSA_PLUGIN_DIR=$L/usr/lib/alsa-lib
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
echo "== devmem?"; ls -la /dev/mem /dev/kmem 2>&1; ls /sys/kernel/debug 2>/dev/null | head -3; ls /proc/audio* /proc/asound/mtsndcard 2>/dev/null
echo "== UL controls"; $AM controls 2>&1 | grep -i 'ADC\|Mic\|Preamp\|Debug\|SideGen\|PGA\|SineTable\|Loopback\|Codec_Loopback' 
echo "== enum items"; for c in 'Audio_MicSource1_Setting' 'Audio_MIC1_Mode_Select' 'Audio_Preamp1_Switch' 'Audio_ADC_1_Switch' 'Audio_PGA1_Setting' 'Audio_Debug_Setting'; do echo -n "$c: "; $AM cget name="$c" 2>&1 | grep -i 'Items\|: values' | tr '\n' ' '; echo; done
echo "== set"; $AM cset name='Audio_MicSource1_Setting' ADC1 2>&1 | tail -1; $AM cset name='Audio_MIC1_Mode_Select' ACCMODE 2>&1 | tail -1; $AM cset name='Audio_Preamp1_Switch' IN_ADC1 2>&1 | tail -1; $AM cset name='Audio_PGA1_Setting' 3 2>&1 | tail -1; $AM cset name='Audio_ADC_1_Switch' On 2>&1 | tail -1
dmesg -c >/dev/null 2>&1
( sleep 1.5; $AM cset name='Audio_Debug_Setting' On >/dev/null 2>&1; sleep 0.2; $AM cset name='Audio_Debug_Setting' Off >/dev/null 2>&1 ) &
$A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 4 /tmp/vendor_cap2.wav 2>&1 | grep -v tzdata | tail -1
dmesg > /tmp/vendor_dmesg2.txt; wc -l /tmp/vendor_dmesg2.txt; grep -c 'AFE_\|AUDIO_TOP' /tmp/vendor_dmesg2.txt
$AM cset name='Audio_ADC_1_Switch' Off >/dev/null 2>&1
IN
$ADB push /tmp/inner2.sh /tmp/inner2.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner2.sh' 2>&1
$ADB pull /tmp/vendor_cap2.wav /root/douglas-flash/vendor_cap2.wav 2>&1 | tail -1; $ADB pull /tmp/vendor_dmesg2.txt /root/douglas-flash/vendor_dmesg2.txt 2>&1 | tail -1
