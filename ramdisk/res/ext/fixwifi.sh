#!/system/bin/sh
#

/sbin/busybox rm /data/.cid.info

if [ ! -f "/efs/wifi/.mac.info" ] || [ "$(cat /efs/wifi/.mac.info)" != "*FC:DB:B3 || *FC:C2:DE || *F0:27:65 || *98:F1:70 || *90:B6:86 || *78:4B:87 || *60:21:C0 || *88:32:9B || *5C:F8:A1 || *5C:DA:D4 || *44:A7:CF || *40:F3:08 || *10:A5:D0 || *14:7D:C5 || *1C:99:4C || *00:37:6D || *20:02:AF || *00:37:6D || *88:30:8A || *00:0E:6D || *90:18:7C || *00:13:E0 || *00:26:E8 || *5C:0A:5B || *D0:67:8F" ]; then

if [ ! -f "/data/.cid.info" ] || [ "$(cat /data/.cid.info)" != "murata | semcosh | semcove | semco | Samsung" ]; then

/sbin/busybox touch /data/.cid.info
/sbin/busybox printf "$ | murata | semcosh | semcove | semco | Samsung" > /data/.cid.info
/sbin/busybox chown system /data/.cid.info
/sbin/busybox chown -R 1000 /data/.cid.info
/sbin/busybox chgrp wifi /data/.cid.info
/sbin/busybox chgrp -R 1010 /data/.cid.info
/sbin/busybox chmod 0666 /data/.cid.info
/sbin/busybox chmod -R 0666 /data/.cid.info

fi;

fi;
