ADB=/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb
sleep 20; $ADB kill-server >/dev/null 2>&1; for i in $(seq 1 30); do sleep 3; $ADB devices 2>/dev/null | grep -q recovery && break; done; $ADB devices
$ADB shell 'mknod /dev/mem c 1 1 2>&1; ls -la /dev/mem; dd if=/dev/mem bs=4 count=1 skip=$((0x10003208/4)) 2>&1 | od -An -tx4; echo rc=$?'
