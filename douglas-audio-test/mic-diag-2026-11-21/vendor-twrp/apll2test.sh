uptime
rec() { timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/a_$1.wav 2>/dev/null; python3 /root/ana.py /root/a_$1.wav; }
echo "== baseline"; rec base
echo "== T1: AFE gates 22M/24M/apll tuners off (TOP_CON0 |= bits 8,9,18,19)"; python3 /root/afew.py 0 80fd4338; rec gate2224; python3 /root/afew.py 0 80fd4038 >/dev/null
echo "== T2: topckgen CLK_CFG_6 = vendor 0x81818181 (gate aud_1_sel/aud_2_sel) + update"; python3 /root/pw.py 10000000 0a0 81818181; python3 /root/pw.py 10000000 004 06000000; rec mux_gated; python3 /root/pw.py 10000000 0a0 01010101 >/dev/null; python3 /root/pw.py 10000000 004 06000000 >/dev/null
echo "== T3: both"; python3 /root/afew.py 0 80fd4338 >/dev/null; python3 /root/pw.py 10000000 0a0 81818181 >/dev/null; python3 /root/pw.py 10000000 004 06000000 >/dev/null; rec both; python3 /root/afew.py 0 80fd4038 >/dev/null; python3 /root/pw.py 10000000 0a0 01010101 >/dev/null; python3 /root/pw.py 10000000 004 06000000 >/dev/null
echo "== T4: AUD2PLL off (APMIXED 0x2b4 bit0 clear) "; python3 /root/pw.py 1000c000 2b4; 
