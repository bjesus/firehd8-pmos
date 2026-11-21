cat > /root/pw.py <<'PY'
import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
base=int(sys.argv[1],16); off=int(sys.argv[2],16)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=base)
if len(sys.argv)>3: struct.pack_into("<I", m, off, int(sys.argv[3],16))
print("%08x=%08x" % (base+off, struct.unpack_from("<I", m, off)[0]))
PY
rec() { timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/s_$1.wav 2>/dev/null; python3 /root/ana.py /root/s_$1.wav; }
sync; echo "T-A: AUD_DIV0/1/2 = vendor (0x103,3,3)"; python3 /root/pw.py 10000000 110 103; python3 /root/pw.py 10000000 114 3; python3 /root/pw.py 10000000 118 3; rec divs
python3 /root/pw.py 10000000 110 101 >/dev/null; python3 /root/pw.py 10000000 114 1 >/dev/null; python3 /root/pw.py 10000000 118 1 >/dev/null
sync; echo "T-B: AP_PLL_CON5 = 0"; python3 /root/pw.py 1000c000 014 0; rec pllcon5; python3 /root/pw.py 1000c000 014 9e
sync; echo "T-C: AP_PLL_CON5=0 + divs together"; python3 /root/pw.py 1000c000 014 0 >/dev/null; python3 /root/pw.py 10000000 110 103 >/dev/null; python3 /root/pw.py 10000000 114 3 >/dev/null; python3 /root/pw.py 10000000 118 3 >/dev/null; rec both; python3 /root/pw.py 1000c000 014 9e >/dev/null; python3 /root/pw.py 10000000 110 101 >/dev/null; python3 /root/pw.py 10000000 114 1 >/dev/null; python3 /root/pw.py 10000000 118 1 >/dev/null
echo "test: TOP_DCMCTL=0x70" > /root/dcm_last_test.txt; sync
echo "T-D: INFRA_TOPCKGEN_DCMCTL = 0x70"; python3 /root/pw.py 10001000 010 70; rec dcmctl; sync
echo "survived" >> /root/dcm_last_test.txt; sync
python3 /root/pw.py 10001000 010 0
