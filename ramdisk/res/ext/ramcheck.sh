#!/sbin/busybox sh
#

/sbin/busybox renice 19 `pidof ramcheck.sh`
FREE=`free -m | grep -i mem | awk '{print $4}'`  

while [ 1 ];
do
	if [ $FREE -lt 15360 ]; then
		echo "2853,5632,24576,86016,96768,96768" > /sys/module/lowmemorykiller/parameters/minfree
		echo "111" > /proc/sys/vm/vfs_cache_pressure
	else
		echo "2853,5166,12288,21920,38678,73216" > /sys/module/lowmemorykiller/parameters/minfree
		echo "111" > /proc/sys/vm/vfs_cache_pressure
	fi
sleep 3
done




