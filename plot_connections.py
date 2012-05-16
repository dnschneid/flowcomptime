'''
Plot active connections vs. time
'''

from util.helper import *

parser = argparse.ArgumentParser()
parser.add_argument('--files', '-f',
                    help="Queue timeseries output to one plot",
                    required=True,
                    action="store",
                    nargs='+',
                    dest="files")

parser.add_argument('--legend', '-l',
                    help="Legend to use if there are multiple plots.  File names used as default.",
                    action="store",
                    nargs="+",
                    default=None,
                    dest="legend")

parser.add_argument('--out', '-o',
                    help="Output png file for the plot.",
                    default=None, # Will show the plot
                    dest="out")

args = parser.parse_args()

to_plot=[]
all=[]
for f in args.files:
    data = {}
    for p in sorted(os.listdir(f)):
        if not p.startswith('receiver'): continue
        receiver_plot = []
        for l in open(os.path.join(f, p), 'r'):
            x = float(l.split()[0])
            y = int(l.split()[2])
            receiver_plot.append((x, y, len(to_plot)))
            all.append((x, y, len(to_plot)))
        to_plot.append(receiver_plot)

all.sort()
            
numReceivers = len(to_plot)

connected = []
for i in range(numReceivers):
    connected.append(0)

x_plot = []
y_plot = []
for t in all:
    connected[t[2]] = t[1]
    x_plot.append(t[0])
    y_plot.append(sum(connected))

line, = plt.plot(x_plot, y_plot)
line.set_linestyle('dashed')
line.set_marker('.')
line.set_label('TCP')
    
plt.title("Active flows")
plt.ylabel("Number of Active Flows")
plt.grid()

plt.xlabel("Time [secs]")

#plt.xlim(0, 300)
#plt.ylim(1000, 9000)

if args.legend:
    plt.legend(args.legend)
else:
    plt.legend()

if args.out:
    plt.savefig(args.out)
else:
    plt.show()
