cat > /root/infw.py <<'PY'
import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x10001000)
off=int(sys.argv[1],16)
if len(sys.argv)>2: struct.pack_into("<I", m, off, int(sys.argv[2],16))
print("infra+%03x=%08x" % (off, struct.unpack_from("<I", m, off)[0]))
PY
echo "== infracfg DCM / CG regs now"; for o in 070 074 078 07c 080 084 088 08c 090 094; do python3 /root/infw.py $o; done
ORIG=$(python3 /root/infw.py 074 | cut -d= -f2)
t() { echo "-- $1"; python3 /root/infw.py 074 $2; timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/c.wav 2>/dev/null; python3 /root/ana.py /root/c.wav; }
V=$((0x$ORIG))
t "audio DCM bit29 cleared" $(printf %08x $((V & ~(1<<29))))
t "audio DCM bit29 set"     $(printf %08x $((V | (1<<29))))
t "all misc DCM bits (21,22,28,29) cleared" $(printf %08x $((V & ~((1<<29)|(1<<28)|(1<<22)|(1<<21)))))
t "peri dcm clkoff/clkslow (bits0,1) cleared + misc cleared" $(printf %08x $((V & ~((1<<29)|(1<<28)|(1<<22)|(1<<21)|3))))
t "force_on bit4 set" $(printf %08x $((V | (1<<4))))
python3 /root/infw.py 074 $ORIG
echo "-- infra bus dcm 0x70: try clearing clkoff/clkslow"
O70=$(python3 /root/infw.py 070 | cut -d= -f2); python3 /root/infw.py 070 $(printf %08x $((0x$O70 & ~0x1f))); timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/c.wav 2>/dev/null; python3 /root/ana.py /root/c.wav; python3 /root/infw.py 070 $O70
