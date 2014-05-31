#!/sbin/busybox sh
#
# libera pagecache cada 3 horas si esta está por debajo de 20360 kbytes
# 

/sbin/busybox renice 19 `pidof libera_ram.sh`
FREE=`free -m | grep -i mem | awk '{print $4}'`  

while [ 1 ];
do
        if [ $FREE -lt 20360 ]; then
                sync
                echo "3" > /proc/sys/vm/drop_caches
                sleep 1
                echo "0" > /proc/sys/vm/drop_caches
        fi
sleep 10800
done
