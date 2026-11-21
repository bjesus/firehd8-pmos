ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner11.sh <<'IN'
[ -c /dev/mem ] || mknod /dev/mem c 1 1
echo "== devmem read test (PERICFG 0x200..):"; dd if=/dev/mem bs=4 count=4 skip=$((0x10003200/4)) 2>/tmp/dderr | od -x | head -2; cat /tmp/dderr | tail -1
dumpall() { tag=$1; mkdir -p /tmp/vb_$tag; for b in DEVAPC_AO:1000e000 DEVAPC:10207000 EMI:10203000 PERICFG:10003000 M4U1:10214000 M4U0:10205000 SMI_COMMON:14017000 INFRA_AO:10001000 TOPCKGEN:10000000 APMIXED:1000c000 MCUCFG:10200000 SPM:10006000 AFE:11220000; do n=${b%%:*}; a=${b##*:}; dd if=/dev/mem bs=4096 count=1 skip=$((0x$a/4096)) of=/tmp/vb_$tag/$n.bin 2>/dev/null || echo "fail $n"; done; ls /tmp/vb_$tag | wc -l; }
dumpall idle
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
$AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1
( sleep 1.5; dumpall cap ) &
$A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 4 /tmp/t_vb.wav 2>&1 | grep -v 'tzdata\|Recording'
$AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1
cd /tmp && tar czf vb.tgz vb_idle vb_cap && ls -la vb.tgz
IN
$ADB push /tmp/inner11.sh /tmp/inner11.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner11.sh' 2>&1
$ADB pull /tmp/vb.tgz /root/douglas-flash/vb.tgz 2>&1 | tail -1
