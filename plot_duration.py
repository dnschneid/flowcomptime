'''
Plot average flow duration vs. flow size
'''

from util.helper import *

def median(L):
    size = len(L)
    if size % 2 == 1:
        return L[(size - 1) / 2]
    else:
        return (L[size / 2 - 1] + L[size / 2]) / 2

def average(L):
    return sum(L) / len(L)

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
data = {}
for f in args.files:
    data = {}
    for p in os.listdir(f):
        if not p.startswith('sender_h'): continue
        for l in open(os.path.join(f, p), 'r'):
            fields = l.split()
            if len(fields) > 2:
                x = int(fields[0])
                y = float(fields[2])
                if x in data:
                    data[x].append(y)
                else:
                    data[x] = [y]
            
x_plot = []
y_min = []
y_max = []
y_median = []
y_average = []

for t in sorted(data.items()):
    x_plot.append(t[0])
    y_min.append(min(t[1]))
    y_max.append(max(t[1]))
    y_median.append(median(t[1]))
    y_average.append(average(t[1]))

#line_median, = plt.plot(x_plot, y_median)
#line_median.set_linestyle('dashed')
#line_median.set_marker('+')
#line_median.set_label('median')

line_average, = plt.plot(x_plot, y_average)
line_average.set_linestyle('dashed')
line_average.set_marker('+')
line_average.set_label('TCP average')

#line_min, = plt.plot(x_plot, y_min)
#line_min.set_linestyle('dashed')
#line_min.set_marker('-')
#line_min.set_label('min')

#line_max, = plt.plot(x_plot, y_max)
#line_max.set_linestyle('dashed')
#line_max.set_marker('-')
#line_max.set_label('max')

plt.title("Flow durations")
plt.ylabel("Median Flow Duration [secs]")
plt.grid()
plt.yscale('log')

plt.xlabel("Flow Size [pkts]")

plt.xlim(0, 10000)
#plt.ylim(0.1, 100)

if args.legend:
    plt.legend(args.legend)
else:
    plt.legend()

if args.out:
    plt.savefig(args.out)
else:
    plt.show()
