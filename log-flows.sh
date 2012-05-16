#!/bin/sh
set -e
dpkg --compare-versions "`ovs-dpctl -V | awk '{print $NF; exit}'`" ge 1.4.0
a=0
while sleep $a; do
    ovs-dpctl show s0 &
    a=1
done 2>/dev/null | grep flows
