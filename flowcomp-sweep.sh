#!/bin/bash

# Exit on any failure
set -e

# Check for uninitialized variables
set -o nounset

ctrlc() {
    killall -9 python
    mn -c
    exit
}

trap ctrlc SIGINT

start=`date`
exptid=`date +%b%d-%H:%M`

rootdir=flowcomp-$exptid
plotpath=.

bw=100

if [ ! -r util/helper.py ]; then
    git submodule init
    git submodule update
fi

make

if dpkg --compare-versions "`ovs-dpctl -V | awk '{print $NF; exit}'`" ge 1.4.0; then
    can_log_flows=y
else
    can_log_flows=n
    echo 'WARNING: Open vSwitch is less than version 1.4.0, so flow count will not be logged.'
    sleep 2
fi

# $1 = totalflows
# $2 = nhosts
# $3 = seconds
# $4 = flowsize
run() {
    echo "$1 flows, $2 hosts, $3 seconds, $4 mean flow size"
    dir=$rootdir/$1f-$2h-$4b-$3s

    python flowcomp.py --bw-host $bw \
        --bw-net $bw \
        --delay 50 \
        --dir $dir \
        --nflows $1 \
        --nhosts $2 \
        --flowsize $4 \
        --sleeptime $3

    python $plotpath/plot_duration.py -f $dir -o $dir/duration.png
    python $plotpath/plot_connections.py -f $dir -o $dir/connections.png
    python $plotpath/plot_utilization.py -f $dir -o $dir/utilization.png -b $bw
    python $plotpath/plot_cpu.py -f $dir -o $dir/cpu.png
    [ "$can_log_flows" = y ] && \
        python $plotpath/plot_flows.py -f $dir -o $dir/flows.png
    sleep 60
}

run 100 10 1800 60
run 500 50 3600 60
run 1000 100 1800 30

echo "Started at" $start
echo "Ended at" `date`
