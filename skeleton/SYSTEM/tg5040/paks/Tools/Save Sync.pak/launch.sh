#!/bin/sh

cd "$(dirname "$0")"

# Same menu cap as the other tool paks.
echo 1008000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./savesync.elf menu
