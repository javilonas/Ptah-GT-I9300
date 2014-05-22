#!/sbin/busybox sh
#
# script optimi_remount.sh - by Javilonas
#

# Remontar todas las particiones con noatime
for k in $(mount | grep relatime | cut -d " " -f3)
do
sync
mount -o remount,noatime,nodiratime,noauto_da_alloc,barrier=0 $k
done

# Remontar y optimizar particiones con EXT4
for k in $(mount | grep ext4 | cut -d " " -f3)
do
sync
mount -o remount,commit=15 $k
done
