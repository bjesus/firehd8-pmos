#!/bin/sh
# Builds the douglas root filesystem image. Runs as root inside an aarch64 Alpine
# container (docker --platform linux/arm64, QEMU user emulation on x86 hosts) with the
# repository mounted at /work, the extracted kernel artifact at /work/dist-kernel
# (containing mods/) and an output directory at /out.
set -eu
ALPINE_BRANCH=${ALPINE_BRANCH:-v3.21}
MIRROR=${MIRROR:-https://dl-cdn.alpinelinux.org/alpine}
IMG_SIZE=${IMG_SIZE:-3G}
SRC=/work
R=/rootfs
OUT=/out

apk add --no-cache -q e2fsprogs tar gzip
mkdir -p "$R/etc/apk/keys" "$R/proc" "$R/sys" "$R/dev" "$R/run" "$R/tmp"
cp /etc/apk/keys/* "$R/etc/apk/keys/"
printf "%s\n" "$MIRROR/$ALPINE_BRANCH/main" "$MIRROR/$ALPINE_BRANCH/community" > "$R/etc/apk/repositories"
PKGS=$(grep -v "^#" "$SRC/tools/rootfs-packages.txt" | tr "\n" " ")
# shellcheck disable=SC2086
apk --root "$R" --initdb --arch aarch64 --repositories-file "$R/etc/apk/repositories" \
    --keys-dir "$R/etc/apk/keys" --no-cache add $PKGS

# douglas-specific files: PID 1 (/sbin/douglas-init), session start, hooks, firmware
cp -a "$SRC/rootfs/." "$R/"
# the checkout belongs to the runner user; everything we overlay belongs to root
for d in "$SRC"/rootfs/*; do chown -R 0:0 "$R/$(basename "$d")"; done
chmod 755 "$R/sbin/douglas-init" "$R"/usr/local/sbin/*
mkdir -p "$R/etc/firmware" && cp "$R"/lib/firmware/* "$R/etc/firmware/"
# kernel modules loaded by start-phosh / init-tier1.sh
rm -rf "$R/root/mods" && cp -a "$SRC/dist-kernel/mods" "$R/root/mods"
rm -f "$R"/root/*.log

echo douglas > "$R/etc/hostname"
echo "127.0.0.1 localhost douglas" > "$R/etc/hosts"
: > "$R/etc/douglas-no-deadman"     # the dev-only "return to TWRP after 90 s" switch stays off
rm -f "$R/etc/machine-id" "$R/etc/resolv.conf"

# user account: user/douglas, wheel => passwordless sudo (etc/sudoers.d/wheel), ssh via dropbear
chroot "$R" adduser -D -u 1000 -s /bin/sh -g "Fire HD 8 user" user
for g in wheel audio input video seat plugdev netdev; do
  chroot "$R" getent group "$g" >/dev/null || chroot "$R" addgroup -S "$g"
  chroot "$R" addgroup user "$g"
done
grep -E "^(seat|wheel|video|input|audio|plugdev):" "$R/etc/group"
echo "user:douglas" | chroot "$R" chpasswd
chroot "$R" passwd -l root >/dev/null
chown -R 1000:1000 "$R/home/user"
chmod 700 "$R/home/user"
chroot "$R" dconf update

rm -rf "$R/var/cache/apk"/* "$R/root/.cache"
# sanity checks that would otherwise only show up on the device
stat -c "%U:%G %a %n" "$R/sbin/douglas-init" "$R/etc/phosh/phoc.ini" "$R/home/user" "$R/root/mods"
[ "$(stat -c %u "$R/sbin/douglas-init")" = 0 ] || { echo "overlay not owned by root"; exit 1; }
grep -q '^user:\$' "$R/etc/shadow" || { echo "user has no password hash"; exit 1; }
grep -q '^seat:.*user' "$R/etc/group" || { echo "user not in seat group"; exit 1; }
du -sh "$R" | sed "s|$R|rootfs|"

# ext4 image, populated without loop devices or root on the host; grows to the whole
# partition on first boot (douglas-init runs resize2fs). orphan_file/metadata_csum_seed
# are turned off so the TWRP kernel (3.18) can still mount it.
rm -f "$OUT/douglas-rootfs.img"
truncate -s "$IMG_SIZE" "$OUT/douglas-rootfs.img"
mke2fs -q -t ext4 -L douglas-root -m 1 -O ^orphan_file,^metadata_csum_seed -E lazy_itable_init=0,lazy_journal_init=0 -d "$R" "$OUT/douglas-rootfs.img"
# mke2fs -d drops setuid/setgid bits (sudo, busybox-suid, ...): put them back
find "$R" -xdev -type f -perm /6000 | while read -r f; do
  printf 'set_inode_field %s mode 0%o\n' "${f#"$R"}" "0x$(stat -c %f "$f")"
done > /tmp/suid.cmds
cat /tmp/suid.cmds
debugfs -w -f /tmp/suid.cmds "$OUT/douglas-rootfs.img" >/dev/null
debugfs -R "stat /usr/bin/sudo" "$OUT/douglas-rootfs.img" 2>/dev/null | grep -q "Mode:  0*4755" || { echo "sudo is not setuid in the image"; exit 1; }
e2fsck -fn "$OUT/douglas-rootfs.img" >/dev/null
gzip -9 -n "$OUT/douglas-rootfs.img"
ls -l "$OUT"
