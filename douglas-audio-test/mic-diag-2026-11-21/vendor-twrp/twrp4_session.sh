ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner4.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
D=/sys/kernel/debug/mtksocaudio; DA=/sys/kernel/debug/mtksocanaaudio
ulon() { $AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1; }
uloff() { $AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1; }
rec() { $A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 2 /tmp/t_$1.wav 2>&1 | grep -v 'tzdata\|Recording'; }
t() { name=$1; shift; ulon; for w in "$@"; do echo "$w" > $D; done; sleep 0.2; grep -E "AFE_PMIC_NEWIF_CFG0|ABB_AFE_CON1 |AUDTOP_CON1" $DA | tr '\n' ' '; echo; rec $name; uloff; }
echo "== T0 control (vendor state)"; t ctrl
echo "== T1 NEWIF_CFG0=0x8330"; t cfg0 "Setanareg addr 0x4024 value 0x8330"
echo "== restore"; echo "Setanareg addr 0x4024 value 0x9330" > $D
echo "== T2 ABB_AFE_CON1=0x1a"; t con1 "Setanareg addr 0x4002 value 0x001a"
echo "Setanareg addr 0x4002 value 0x0010" > $D
echo "== T3 AUDTOP_CON1=0x300"; t top1 "Setanareg addr 0x0702 value 0x0300"
echo "Setanareg addr 0x0702 value 0x0320" > $D
echo "== T4 our leftover AFE values"; t afe "Setafereg addr 0x108 value 0x83001802" "Setafereg addr 0x10c value 0xf74f0000" "Setafereg addr 0x34 value 0xa08" "Setafereg addr 0x14 value 0xa0a0a" "Setafereg addr 0x24 value 0x200000" "Setafereg addr 0x28 value 0x40" "Setafereg addr 0x6c value 0x19e000"
echo "Setafereg addr 0x108 value 0x0f000000" > $D; echo "Setafereg addr 0x10c value 0" > $D; echo "Setafereg addr 0x34 value 0" > $D; echo "Setafereg addr 0x24 value 0" > $D; echo "Setafereg addr 0x28 value 0" > $D; echo "Setafereg addr 0x6c value 0x19e018" > $D
echo "== T5 control again"; t ctrl2
ls -la /tmp/t_*.wav
IN
$ADB push /tmp/inner4.sh /tmp/inner4.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner4.sh' 2>&1
for f in ctrl cfg0 con1 top1 afe ctrl2; do $ADB pull /tmp/t_$f.wav /root/douglas-flash/t_$f.wav >/dev/null 2>&1; done; echo pulled
