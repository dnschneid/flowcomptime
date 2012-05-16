'''
Plot flows vs. time
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

x_plot=[]
y_plot=[]
for f in args.files:
    for p in sorted(os.listdir(f)):
        if not p.startswith('flows'): continue
        x = 0
        for l in open(os.path.join(f, p), 'r'):
            y = int(l.split()[-1])
            y_plot.append(y)
            x_plot.append(x)
            x += 1

line, = plt.plot(x_plot, y_plot)
line.set_marker('.')
line.set_label('OpenVS Flows')

plt.title("OpenVS Active Flows")
plt.ylabel("Flows")
plt.grid()

plt.xlabel("Time [secs]")

#plt.xlim(0, 300)
#plt.ylim(1000, 9000)

if args.legend:
    plt.legend(args.legend)
else:
    plt.legend(loc='lower right')

if args.out:
    plt.savefig(args.out)
else:
    plt.show()
