ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner7.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
G=/sys/kernel/debug; D=$G/mtksocaudio
C=$G/dynamic_debug/control; grep -c 'AudDrv_Afe\|mt_soc' $C
for f in AudDrv_Afe.c AudDrv_Ana.c AudDrv_Clk.c mt_soc_afe_control.c mt_soc_codec_63xx.c mt_soc_pcm_capture.c mt_soc_machine.c mt_soc_afe_connection.c; do echo "file $f +p" > $C 2>/dev/null || echo "fail $f"; done
grep -c ' =p' $C
dmesg -c > /dev/null 2>&1
echo "### GETREGS"; for a in 0x0e4 0x250 0x280 0x284 0x288 0x2a4 0x320 0x338 0x37c 0x3d0 0x3ec 0x480 0x4b0 0x4f0 0x500 0x540 0x00c 0x130 0x134; do echo "Getafereg addr $a" > $D; done; for a in 0x4018 0x401e 0x0102 0x0108 0x010e 0x0126 0x0700 0x0702 0x0704 0x0710 0x0712; do echo "Getanareg addr $a" > $D; done
echo "### ULON"; $AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1
echo "### REC"; $A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 2 /tmp/t_trace.wav 2>&1 | grep -v 'tzdata\|Recording'
echo "### ULOFF"; $AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1
dmesg > /tmp/vendor_trace.txt; wc -l /tmp/vendor_trace.txt; grep -c 'Afe_Set_Reg\|Ana_Set_Reg' /tmp/vendor_trace.txt
for f in AudDrv_Afe.c AudDrv_Ana.c AudDrv_Clk.c mt_soc_afe_control.c mt_soc_codec_63xx.c mt_soc_pcm_capture.c mt_soc_machine.c mt_soc_afe_connection.c; do echo "file $f -p" > $C 2>/dev/null; done
IN
$ADB push /tmp/inner7.sh /tmp/inner7.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner7.sh' 2>&1
$ADB pull /tmp/vendor_trace.txt /root/douglas-flash/vendor_trace.txt 2>&1 | tail -1
