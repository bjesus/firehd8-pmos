import struct,sys
d=open(sys.argv[1],'rb').read()[44:]
s=struct.unpack('<%dh'%(len(d)//2), d)
nz=sum(1 for x in s if x)
print(sys.argv[1], "samples",len(s),"peak",max(abs(x) for x in s),"nonzero %.1f%%"%(100.0*nz/len(s)), "first8", s[:8])
