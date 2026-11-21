ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
cat > /tmp/inner5.sh <<'IN'
L=/data; export ALSA_CONFIG_DIR=$L/usr/share/alsa ALSA_CONFIG_PATH=$L/usr/share/alsa/alsa.conf
A="$L/lib/ld-musl-aarch64.so.1 --library-path $L/usr/lib:$L/lib"; AM="$A $L/usr/bin/amixer -D hw:0"
G=/sys/kernel/debug; O=/tmp/vd; mkdir -p $O
cat $G/clk/clk_summary > $O/clk_summary_idle.txt 2>&1
ls -R $G/m4u > $O/m4u_ls.txt 2>&1; ls -R $G/smi > $O/smi_ls.txt 2>&1
for f in $G/m4u/* $G/smi/*; do [ -f "$f" ] && { echo "### $f"; timeout 3 cat "$f" 2>&1 | head -200; } ; done > $O/m4u_smi_dump.txt 2>&1
ls $G/regmap > $O/regmap_ls.txt 2>&1; for r in $G/regmap/*; do echo "### $r"; cat $r/name 2>/dev/null; done > $O/regmap_names.txt 2>&1
P=$(ls -d $G/regmap/*pwrap* $G/regmap/*mt6323* 2>/dev/null | head -1); echo "pwrap regmap: $P"; [ -n "$P" ] && cat $P/registers > $O/pmic_all_idle.txt 2>&1
cat $G/gpio > $O/gpio_idle.txt 2>&1; cat $G/pinctrl/*/pins > $O/pins_idle.txt 2>&1
$AM cset name='Audio_MicSource1_Setting' 0 >/dev/null 2>&1; $AM cset name='Audio_MIC1_Mode_Select' 0 >/dev/null 2>&1; $AM cset name='Audio_Preamp1_Switch' 1 >/dev/null 2>&1; $AM cset name='Audio_ADC_1_Switch' 1 >/dev/null 2>&1
( sleep 1.5; cat $G/clk/clk_summary > $O/clk_summary_cap.txt 2>&1; [ -n "$P" ] && cat $P/registers > $O/pmic_all_cap.txt 2>&1; cat $G/gpio > $O/gpio_cap.txt 2>&1; cat $G/pinctrl/*/pins > $O/pins_cap.txt 2>&1; cat $G/mtksocaudio > $O/afe_cap2.txt 2>&1 ) &
$A $L/usr/bin/arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 5 /tmp/t_cap5.wav 2>&1 | grep -v 'tzdata\|Recording'
$AM cset name='Audio_ADC_1_Switch' 0 >/dev/null 2>&1
cat /proc/iomem > $O/iomem.txt; cat /proc/interrupts > $O/interrupts.txt 2>/dev/null
cd /tmp && tar czf vd.tgz vd && ls -la vd.tgz; ls vd | tr '\n' ' '
IN
$ADB push /tmp/inner5.sh /tmp/inner5.sh >/dev/null 2>&1; $ADB shell 'sh /tmp/inner5.sh' 2>&1
$ADB pull /tmp/vd.tgz /root/douglas-flash/vd.tgz 2>&1 | tail -1
