Base: https://github.com/aslater3/LibreEcho-Linux-6.1 commit 23721f56408fb4c4fe1a368abf06caa98edaecfa — branch `douglas` on https://github.com/bjesus/LibreEcho-Linux-6.1 (2 commits); 0001-douglas-port.patch holds both as a mailbox, douglas-port-full.diff is the squashed diff.
Apply: git am 0001-douglas-port.patch; cp douglas-6.1.config <O>/.config
CONFIG_INITRAMFS_SOURCE in the config must point at ../douglas-initfs (see HANDOFF.md section 5).
Branch form of the same port: https://github.com/bjesus/LibreEcho-Linux-6.1/tree/douglas (commit 83a78c5ba).
