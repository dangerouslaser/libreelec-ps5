#!/bin/sh
# Run CB1 LibreELEC's Kodi from its SYSTEM image in a chroot on the PS5's
# Ubuntu, using the host kernel. /storage persists in /opt/le/storage.
set -e
LE=/opt/le
# SYSIMG: the SYSTEM squashfs from a LibreELEC release tar
SYSIMG=${SYSIMG:-$(ls /opt/le/LibreELEC-*/target/SYSTEM | tail -1)}
# MEDIA: a host directory to show inside Kodi as /media/media
MEDIA=${MEDIA:-/mnt/media}
TZ=${TZ:-$(cat /etc/timezone 2>/dev/null || echo UTC)}
R=$LE/root

mkdir -p $LE/sys $LE/upper $LE/work $LE/storage $R
mountpoint -q $LE/sys || mount -o loop,ro "$SYSIMG" $LE/sys
mountpoint -q $R || mount -t overlay overlay -o lowerdir=$LE/sys,upperdir=$LE/upper,workdir=$LE/work $R
mountpoint -q $R/dev  || mount --rbind /dev $R/dev
mountpoint -q $R/proc || mount -t proc proc $R/proc
mountpoint -q $R/sys  || mount --rbind /sys $R/sys
mountpoint -q $R/run  || mount -t tmpfs tmpfs $R/run
# LibreELEC keeps /var and /tmp in RAM; /media links to /var/media.
mountpoint -q $R/var  || mount -t tmpfs tmpfs $R/var
mountpoint -q $R/tmp  || mount -t tmpfs tmpfs $R/tmp
mkdir -p $R/var/media/media $R/var/log $R/var/run
mkdir -p $R/run/udev $R/run/libreelec
mountpoint -q $R/run/udev || mount --bind /run/udev $R/run/udev
mountpoint -q $R/storage || mount --bind $LE/storage $R/storage
if mountpoint -q $MEDIA && ! mountpoint -q $R/var/media/media; then
	mount --bind $MEDIA $R/var/media/media
fi
# Binds of shared host mounts (/sys, /dev) join the host's peer groups. Make
# the whole tree a slave so unmounting the chroot never propagates back and
# restarts never stack binds onto the host.
mount --make-rslave $R
cp /etc/resolv.conf $R/etc/resolv.conf 2>/dev/null || true

exec chroot $R /usr/bin/env -i TZ=$TZ HOME=/storage KODI_TEMP=/storage/.kodi/temp \
	KODI_HOME=/usr/share/kodi/ MALLOC_MMAP_THRESHOLD_=524288 \
	PATH=/usr/bin:/usr/sbin:/bin:/sbin LANG=C.UTF-8 \
	/bin/sh -c '/usr/lib/kodi/kodi-config; exec /usr/lib/kodi/kodi.bin --standalone -fs --audio-backend=alsa --windowing=gbm'
