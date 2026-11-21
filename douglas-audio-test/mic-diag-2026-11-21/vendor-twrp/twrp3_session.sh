ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner3.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
mount -t debugfs none /sys/kernel/debug 2>/dev/null; echo "== debugfs entries"; ls /sys/kernel/debug | tr '\n' ' '; echo
F=$(ls /sys/kernel/debug | grep -i 'mtksocaudio\|soc_audio\|mtaudio' | grep -vi ana | head -1); FA=$(ls /sys/kernel/debug | grep -i 'ana' | grep -i 'aud\|mtk' | head -1); echo "afe dbg=$F  ana dbg=$FA"
echo "== IDLE dumps"; cat /sys/kernel/debug/$F > /tmp/vendor_afe_idle.txt 2>&1; cat /sys/kernel/debug/$FA > /tmp/vendor_ana_idle.txt 2>&1; wc -l /tmp/vendor_afe_idle.txt /tmp/vendor_ana_idle.txt
$AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_PGA1_Setting' 3 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1
( sleep 2; cat /sys/kernel/debug/$F > /tmp/vendor_afe_cap.txt 2>&1; cat /sys/kernel/debug/$FA > /tmp/vendor_ana_cap.txt 2>&1 ) &
$A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 5 /tmp/vendor_cap3.wav 2>&1 | grep -v tzdata | tail -1
$AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1
wc -l /tmp/vendor_afe_cap.txt /tmp/vendor_ana_cap.txt; head -5 /tmp/vendor_afe_cap.txt
IN
$ADB push /tmp/inner3.sh /tmp/inner3.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner3.sh' 2>&1
for f in vendor_afe_idle.txt vendor_ana_idle.txt vendor_afe_cap.txt vendor_ana_cap.txt vendor_cap3.wav; do $ADB pull /tmp/$f /root/douglas-flash/$f 2>&1 | tail -1; done
