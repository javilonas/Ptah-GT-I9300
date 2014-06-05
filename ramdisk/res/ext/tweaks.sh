#!/sbin/busybox sh
#
# Tweaks - by Javilonas
#

# CACHE AUTO CLEAN
sync
echo "3" > /proc/sys/vm/drop_caches
sleep 1
echo "0" > /proc/sys/vm/drop_caches

#disable cpuidle log
echo "0" > /sys/module/cpuidle_exynos4/parameters/log_en

#Mali 400MP GPU threshold
echo "40% 32% 60% 55% 60% 55% 60% 55%" > /sys/class/misc/gpu_control/gpu_clock_control

# Tweak kernel scheduler, less aggressive settings
echo "500000" > /proc/sys/kernel/sched_min_granularity_ns
echo "1000000" > /proc/sys/kernel/sched_latency_ns
echo "100000" > /proc/sys/kernel/sched_wakeup_granularity_ns

# Miscellaneous tweaks
echo "524488" > /proc/sys/fs/file-max
echo "33200" > /proc/sys/fs/inotify/max_queued_events
echo "584" > /proc/sys/fs/inotify/max_user_instances
echo "10696" > /proc/sys/fs/inotify/max_user_watches
echo "0" > /proc/sys/vm/block_dump
echo "5" > /proc/sys/vm/laptop_mode
echo "0" > /proc/sys/vm/panic_on_oom 
echo "8" > /proc/sys/vm/page-cluster
echo "10" > /proc/sys/fs/lease-break-time
echo "65836" > /proc/sys/kernel/msgmni
echo "65836" > /proc/sys/kernel/msgmax
echo "512 512000 256 2048" > /proc/sys/kernel/sem
echo "268535656" > /proc/sys/kernel/shmmax
echo "525488" > /proc/sys/kernel/threads-max
echo "1" > /proc/sys/vm/oom_kill_allocating_task

# Tweaks internos
echo "2" > /sys/devices/system/cpu/sched_mc_power_savings
echo "0" > /proc/sys/kernel/randomize_va_space
echo "3" > /sys/module/cpuidle_exynos4/parameters/enable_mask

# pegasusq tweaks
echo "20000" > /sys/devices/system/cpu/cpufreq/pegasusq/sampling_rate
echo "10" > /sys/devices/system/cpu/cpufreq/pegasusq/cpu_up_rate
echo "10" > /sys/devices/system/cpu/cpufreq/pegasusq/cpu_down_rate
echo "500000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_1_1
echo "400000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_2_0
echo "800000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_2_1
echo "600000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_3_0
echo "800000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_3_1
echo "600000" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_freq_4_0
echo "200" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_1_1
echo "200" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_2_0
echo "300" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_2_1
echo "300" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_3_0
echo "400" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_3_1
echo "400" > /sys/devices/system/cpu/cpufreq/pegasusq/hotplug_rq_4_0
echo "2" > /sys/devices/system/cpu/cpufreq/pegasusq/sampling_down_factor
echo "37" > /sys/devices/system/cpu/cpufreq/pegasusq/freq_step
echo "85" > /sys/devices/system/cpu/cpufreq/pegasusq/up_threshold

# IPv6 privacy tweak
echo "2" > /proc/sys/net/ipv6/conf/all/use_tempaddr

# TCP tweaks
echo "1" > /proc/sys/net/ipv4/tcp_low_latency
echo "1" > /proc/sys/net/ipv4/tcp_timestamps
echo "1" > /proc/sys/net/ipv4/tcp_tw_reuse
echo "1" > /proc/sys/net/ipv4/tcp_sack
echo "1" > /proc/sys/net/ipv4/tcp_dsack
echo "1" > /proc/sys/net/ipv4/tcp_tw_recycle
echo "1" > /proc/sys/net/ipv4/tcp_window_scaling
echo "1" > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
echo "1" > /proc/sys/net/ipv4/route/flush
echo "2" > /proc/sys/net/ipv4/tcp_syn_retries
echo "2" > /proc/sys/net/ipv4/tcp_synack_retries
echo "10" > /proc/sys/net/ipv4/tcp_fin_timeout
echo "2" > /proc/sys/net/ipv4/tcp_ecn
echo "524288" > /proc/sys/net/core/wmem_max
echo "524288" > /proc/sys/net/core/rmem_max
echo "262144" > /proc/sys/net/core/rmem_default
echo "262144" > /proc/sys/net/core/wmem_default
echo "20480" > /proc/sys/net/core/optmem_max
echo "6144 87380 524288" > /proc/sys/net/ipv4/tcp_wmem
echo "6144 87380 524288" > /proc/sys/net/ipv4/tcp_rmem
echo "4096" > /proc/sys/net/ipv4/udp_rmem_min
echo "4096" > /proc/sys/net/ipv4/udp_wmem_min

# reduce txqueuelen to 0 to switch from a packet queue to a byte one
NET=`ls -d /sys/class/net/*`
for i in $NET 
do
echo "0" > $i/tx_queue_len

done

LOOP=`ls -d /sys/block/loop*`
RAM=`ls -d /sys/block/ram*`
MMC=`ls -d /sys/block/mmc*`
ZSWA=`ls -d /sys/block/vnswap*`

for i in $LOOP $RAM $MMC $ZSWA
do 
echo "row" > $i/queue/scheduler
echo "0" > $i/queue/add_random
echo "0" > $i/queue/rotational
echo "8192" > $i/queue/nr_requests
echo "0" > $i/queue/iostats
echo "2" > $i/queue/rq_affinity
echo "1" > $i/queue/iosched/back_seek_penalty
echo "2" > $i/queue/iosched/slice_idle
echo "1" > $i/queue/iosched/low_latency

done

# Turn off debugging for certain modules
echo "0" > /sys/module/wakelock/parameters/debug_mask
echo "0" > /sys/module/userwakelock/parameters/debug_mask
echo "0" > /sys/module/earlysuspend/parameters/debug_mask
echo "0" > /sys/module/alarm/parameters/debug_mask
echo "0" > /sys/module/alarm_dev/parameters/debug_mask
echo "0" > /sys/module/binder/parameters/debug_mask
echo "0" > /sys/module/lowmemorykiller/parameters/debug_level
echo "0" > /sys/module/mali/parameters/mali_debug_level
echo "0" > /sys/module/ump/parameters/ump_debug_level
echo "0" > /sys/module/kernel/parameters/initcall_debug
echo "0" > /sys/module/xt_qtaguid/parameters/debug_mask

# Otros Misc tweaks
/sbin/busybox mount -t debugfs none /sys/kernel/debug
echo NO_NORMALIZED_SLEEPER > /sys/kernel/debug/sched_features
echo NO_GENTLE_FAIR_SLEEPERS > /sys/kernel/debug/sched_features
echo NO_START_DEBIT > /sys/kernel/debug/sched_features
echo NO_WAKEUP_PREEMPT > /sys/kernel/debug/sched_features
echo NEXT_BUDDY > /sys/kernel/debug/sched_features
echo ARCH_POWER > /sys/kernel/debug/sched_features
echo SYNC_WAKEUPS > /sys/kernel/debug/sched_features
echo HRTICK > /sys/kernel/debug/sched_features


busy=/sbin/busybox;

# lmk tweaks for fewer empty background processes
minfree=12288,15360,18432,21504,24576,30720;
lmk=/sys/module/lowmemorykiller/parameters/minfree;
minboot=`cat $lmk`;
while sleep 1; do
  if [ `cat $lmk` != $minboot ]; then
    [ `cat $lmk` != $minfree ] && echo $minfree > $lmk || exit;
  fi;
done &

# wait for systemui and increase its priority
while sleep 1; do
  if [ `$busy pidof com.android.systemui` ]; then
    systemui=`$busy pidof com.android.systemui`;
    $busy renice -18 $systemui;
    $busy echo -17 > /proc/$systemui/oom_adj;
    $busy chmod 100 /proc/$systemui/oom_adj;
    exit;
  fi;
done &

# lmk whitelist for common launchers and increase launcher priority
list="com.android.launcher com.sec.android.app.launcher com.google.android.googlequicksearchbox org.adw.launcher org.adwfreak.launcher net.alamoapps.launcher com.anddoes.launcher com.android.lmt com.chrislacy.actionlauncher.pro com.cyanogenmod.trebuchet com.gau.go.launcherex com.gtp.nextlauncher com.miui.mihome2 com.mobint.hololauncher com.mobint.hololauncher.hd com.qihoo360.launcher com.teslacoilsw.launcher com.teslacoilsw.launcher.prime com.tsf.shell org.zeam";
while sleep 60; do
  for class in $list; do
    if [ `$busy pgrep $class | head -n 1` ]; then
      launcher=`$busy pgrep $class`;
      $busy echo -17 > /proc/$launcher/oom_adj;
      $busy chmod 100 /proc/$launcher/oom_adj;
      $busy renice -18 $launcher;
    fi;
  done;
  exit;
done &

