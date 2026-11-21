import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
base=int(sys.argv[1],16); off=int(sys.argv[2],16)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=base)
if len(sys.argv)>3: struct.pack_into("<I", m, off, int(sys.argv[3],16))
print("%08x=%08x" % (base+off, struct.unpack_from("<I", m, off)[0]))
