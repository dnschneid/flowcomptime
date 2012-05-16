#!/usr/bin/python

"CS244 Assignment 3: Flow Completion Time"

from mininet.topo import Topo
from mininet.node import CPULimitedHost
from mininet.link import TCLink
from mininet.net import Mininet
from mininet.log import lg
from mininet.util import dumpNodeConnections

from time import sleep, time
import termcolor as T
from argparse import ArgumentParser
import os


# Maximum number of flows per instance, as reported by traffic
maxFlows = 0


def cprint(s, color, cr=True):
    """Print in color
       s: string to print
       color: color to use"""
    if cr:
        print T.colored(s, color)
    else:
        print T.colored(s, color),


# Parse arguments

parser = ArgumentParser(description="Buffer sizing tests")
parser.add_argument('--sleeptime', '-t',
                    dest="sleeptime",
                    type=float,
                    action="store",
                    help="Time to execute flows",
                    required=True)

parser.add_argument('--bw-host', '-B',
                    dest="bw_host",
                    type=float,
                    action="store",
                    help="Bandwidth of host links",
                    required=True)

parser.add_argument('--bw-net', '-b',
                    dest="bw_net",
                    type=float,
                    action="store",
                    help="Bandwidth of network link",
                    required=True)

parser.add_argument('--flowsize', '-f',
                    dest="flowsize",
                    type=float,
                    action="store",
                    help="Mean flow size",
                    required=True)

parser.add_argument('--delay',
                    dest="delay",
                    type=float,
                    help="Delay in milliseconds of host links",
                    default=87)

parser.add_argument('--dir', '-d',
                    dest="dir",
                    action="store",
                    help="Directory to store outputs",
                    default="results",
                    required=True)

parser.add_argument('--nhosts',
                    dest="nhosts",
                    type=int,
                    action="store",
                    help="Number of nodes in topology.  Must be >= 2",
                    required=True)

parser.add_argument('--nflows',
                    dest="nflows",
                    action="store",
                    type=int,
                    help="Total number of flows to start",
                    required=True)

parser.add_argument('--maxq',
                    dest="maxq",
                    action="store",
                    help="Max buffer size of network interface in packets",
                    default=1000)

# Expt parameters
args = parser.parse_args()

if not os.path.exists(args.dir):
    os.makedirs(args.dir)

lg.setLogLevel('info')

# Generates the topology to be instantiated in Mininet
class StarTopo(Topo):
    "Star topology for Buffer Sizing experiment"

    def __init__(self, n=3, cpu=None, bw_host=None, bw_net=None,
                 delay=None, maxq=None):
        # Add default members to class.
        super(StarTopo, self ).__init__()
        self.n = n
        self.cpu = cpu
        self.bw_host = bw_host
        self.bw_net = bw_net
        self.delay = delay
        self.maxq = maxq
        self.create_topology()

    # Set appropriate values for bandwidth, delay, and queue size
    def create_topology(self):
        hconfig = {'cpu': self.cpu}
        netlconfig = {'bw': self.bw_net, 'delay': self.delay,
                      'max_queue_size': self.maxq }
        hostlconfig = {'bw': self.bw_host, 'delay': '0ms',
                       'max_queue_size': self.maxq }

        switch = self.add_switch('s0')
        receiver = self.add_host('h0', **hconfig)
        self.add_link(receiver, switch, **netlconfig)
        for i in range(1, self.n):
            host = self.add_host('h%d' % i, **hconfig)
            self.add_link(host, switch, **hostlconfig)

# Starts enough receivers at h0 to handle all of the incoming flows
def start_receivers(net):
    # Determine maxFlows
    global maxFlows
    recvr = net.getNodeByName('h0')
    command = './traffic'
    recvr.cmd('%s > %s/maxflows.txt' % (command, args.dir))
    line = open('%s/maxflows.txt' % args.dir).read()
    maxFlows = int(line.split()[-1])

    numReceivers = 1
    currentPortFlows = 0
    for i in range(args.nhosts - 1):
        numFlows = args.nflows / (args.nhosts - 1)
        remainder = args.nflows % (args.nhosts - 1)
        if i < remainder:
            numFlows += 1
        if currentPortFlows + numFlows > maxFlows:
            print "filled this receiver with"
            print currentPortFlows
            numReceivers += 1
            currentPortFlows = 0
        currentPortFlows += numFlows
    
    cprint('got maxFlows of %d, numReceivers of %d' % (maxFlows, numReceivers),
           "green")
    for i in range(numReceivers):
        command = '%s %d > %s/receiver_%d.txt &' % (
            './traffic', 5001 + i, args.dir, i)
        print command
        recvr.cmd(command)

# Starts nflows flows across the senders
def start_senders(net):
    currentPort = 5001
    currentPortFlows = 0
    for i in range(args.nhosts - 1):
        senderNumber = i + 1
        receiverNumber = 0
        senderName = 'h%d' % senderNumber
        receiverName = 'h%d' % receiverNumber
        sender = net.getNodeByName(senderName)
        receiver = net.getNodeByName(receiverName)
        receiverIp = receiver.IP()

        numFlows = args.nflows / (args.nhosts - 1)
        remainder = args.nflows % (args.nhosts - 1)
        if i < remainder:
            numFlows += 1
        if numFlows > maxFlows:
            cprint("WARNING: numFlows > maxFlows", "red")
        if currentPortFlows + numFlows > maxFlows:
            currentPort += 1
            currentPortFlows = 0
        currentPortFlows += numFlows

        command = '%s %i %s %i %i %i %f > %s/sender_%s_%i.txt 2>> %s/sender_stderr.txt &' % (
            './traffic',
            currentPort, receiverIp, numFlows, 1000, args.flowsize, 1.4, args.dir, senderName,
            i, args.dir)
        print command
        sender.cmd(command)

def start_stats():
    command = './log-flows.sh > %s/flows.txt &' % (args.dir)
    print command
    os.system(command)
    command = 'vmstat -n 1 > %s/vmstat.txt &' % (args.dir)
    print command
    os.system(command)

def main():
    start = time()

    # Generate the topology
    # TODO: generate different topologies here
    topo = StarTopo(n=args.nhosts, bw_host=args.bw_host,
                    delay='%sms' % args.delay,
                    bw_net=args.bw_net, maxq=args.maxq)
    net = Mininet(topo=topo, host=CPULimitedHost, link=TCLink,
                  autoPinCpus=True)
    net.start()
    dumpNodeConnections(net.hosts)

    #cprint("pinging all hosts in topology...", "green")
    #net.pingAll()
    #cprint("done!", "green")

    cprint("starting receivers...", "green")
    start_receivers(net)
    cprint("done!", "green")

    cprint("starting flow logging + vmstat...", "green")
    start_stats()
    cprint("done!", "green")

    cprint("Starting senders...", "green")
    start_senders(net)
    cprint("done!", "green")

    cprint("waiting %f seconds..." % args.sleeptime, "green")
    sleep(args.sleeptime)
    cprint("done!", "green")

    # Shut down processes
    os.system('killall ' + 'traffic vmstat log-flows.sh')

    sleep(5)
    
    net.stop()
    end = time()
    cprint("Took %.3f seconds" % (end - start), "yellow")

if __name__ == '__main__':
    main()
