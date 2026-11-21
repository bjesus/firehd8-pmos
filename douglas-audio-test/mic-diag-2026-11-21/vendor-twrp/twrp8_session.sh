ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner8.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"; D=/sys/kernel/debug/mtksocaudio
ulon() { $AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1; }
uloff() { $AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1; }
rec() { $A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 2 /tmp/t_$1.wav 2>&1 | grep -v 'tzdata\|Recording'; }
echo "T6 0x284=0x8000"; ulon; echo "Setafereg addr 0x284 value 0x8000" > $D; rec r284; uloff; echo "Setafereg addr 0x284 value 0x2000" > $D
echo "T7 0x284=0x8000 written BEFORE ul on"; echo "Setafereg addr 0x284 value 0x8000" > $D; ulon; rec r284b; uloff; echo "Setafereg addr 0x284 value 0x2000" > $D
echo "T8 0xe4=0x09000000"; ulon; echo "Setafereg addr 0xe4 value 0x09000000" > $D; rec re4; uloff
echo "T9 control"; ulon; rec ctrl3; uloff
IN
$ADB push /tmp/inner8.sh /tmp/inner8.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner8.sh' 2>&1
for f in r284 r284b re4 ctrl3; do $ADB pull /tmp/t_$f.wav /root/douglas-flash/t_$f.wav >/dev/null 2>&1; done; echo pulled
