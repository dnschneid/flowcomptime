'''
Plot cpu usage vs. time
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

usr_plot=[]
sys_plot=[]
sum_plot=[]
x_plot=[]
for f in args.files:
    for p in sorted(os.listdir(f)):
        if not p.startswith('vmstat'): continue
        x = 0
        for l in open(os.path.join(f, p), 'r'):
            if l.split()[0] == 'procs': continue
            if l.split()[0] == 'r': continue
            usr = int(l.split()[-4])
            sys = int(l.split()[-3])
            usr_plot.append(usr)
            sys_plot.append(sys)
            x_plot.append(x)
            sum_plot.append(usr + sys)
            x += 1

line_usr, = plt.plot(x_plot, usr_plot)
line_usr.set_marker('.')
line_usr.set_label('User')

line_sys, = plt.plot(x_plot, sys_plot)
line_sys.set_marker('.')
line_sys.set_label('System')

line_sum, = plt.plot(x_plot, sum_plot)
line_sum.set_marker('.')
line_sum.set_label('Total')
    
plt.title("CPU utilization")
plt.ylabel("CPU utilization")
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
