mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
grep -i -E 'aud|apll|a1sys|a2sys|infra.*aud' /sys/kernel/debug/clk/clk_summary
echo "== parents"; for c in aud_intbus_sel audio_sel aud_1_sel aud_2_sel apll1 apll2 aud_mux1_sel aud_mux2_sel; do [ -d /sys/kernel/debug/clk/$c ] && echo "$c -> $(cat /sys/kernel/debug/clk/$c/clk_parent 2>/dev/null) rate=$(cat /sys/kernel/debug/clk/$c/clk_rate) en=$(cat /sys/kernel/debug/clk/$c/clk_enable_count)"; done
echo "== possible parents of aud_intbus_sel / audio_sel"; cat /sys/kernel/debug/clk/aud_intbus_sel/clk_possible_parents 2>/dev/null; cat /sys/kernel/debug/clk/audio_sel/clk_possible_parents 2>/dev/null
