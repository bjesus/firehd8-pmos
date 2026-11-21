mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
echo "== genpd"; cat /sys/kernel/debug/pm_genpd/pm_genpd_summary 2>&1 | head -40
cat > /root/rd.py <<'PY'
import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
base=int(sys.argv[1],16)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
for a in sys.argv[2:]:
    o=int(a,16); print("%08x=%08x" % (base+o, struct.unpack_from("<I", m, o)[0]))
PY
echo "== SPM power con regs (0x10006000)"; python3 /root/rd.py 10006000 29c 2a0 2a4 2a8 2ac 2b0 2b4 2b8 2bc 2c0 2c4 2c8 2cc 2d0 2d4 2d8 2dc 2e0 60c 610 614 618
echo "== infracfg bus protect (0x10001000)"; python3 /root/rd.py 10001000 220 224 228 22c 230
echo "== infracfg clock gates"; python3 /root/rd.py 10001000 040 044 048 090 094 098 0a0 0a4 0a8
