import struct,sys,math
d=open(sys.argv[1],'rb').read()[44:]; s=struct.unpack('<%dh'%(len(d)//2), d)
L=s[0::2]; R=s[1::2]; n=len(L)
def st(x):
    m=sum(x)/len(x); v=math.sqrt(sum((a-m)**2 for a in x)/len(x)); return m,v,max(abs(a) for a in x)
mL,vL,pL=st(L); mR,vR,pR=st(R)
# zero crossings per second on L as a crude pitch indicator
zc=sum(1 for i in range(1,n) if (L[i-1]<0)!=(L[i]<0)); 
print("%s: L mean=%.0f rms=%.0f peak=%d | R mean=%.0f rms=%.0f peak=%d | zero-cross/s=%.0f | distinct values=%d" % (sys.argv[1], mL,vL,pL,mR,vR,pR, zc/(n/48000.0), len(set(s))))
