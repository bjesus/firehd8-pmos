echo "CG sta before:"; python3 /root/pw.py 10001000 090; python3 /root/pw.py 10001000 094
python3 /root/pw.py 10001000 08c 80003800 >/dev/null; echo "CG1 sta after clr:"; python3 /root/pw.py 10001000 090
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/i_cg1.wav 2>/dev/null; python3 /root/ana.py /root/i_cg1.wav
