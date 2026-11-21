sync; echo "test: bit22 (pmic dcm) cleared -> vendor default 0xb01f0603" > /root/dcm_last_test.txt; sync
python3 /root/infw.py 074 b01f0603
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/c22.wav 2>/dev/null; python3 /root/ana.py /root/c22.wav; sync
echo "survived bit22" >> /root/dcm_last_test.txt; sync
python3 /root/infw.py 074 b05f0603
echo "test: bit29 (audio dcm) cleared alone -> 0x905f0603" >> /root/dcm_last_test.txt; sync
python3 /root/infw.py 074 905f0603
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/c29.wav 2>/dev/null; python3 /root/ana.py /root/c29.wav; sync
echo "survived bit29" >> /root/dcm_last_test.txt; sync
python3 /root/infw.py 074 b05f0603
