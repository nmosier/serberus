import argparse as ap
import shutil
import os
from collections import defaultdict
import json
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import math
from types import SimpleNamespace
import numpy as np
from matplotlib.patches import Rectangle
import matplotlib.patheffects as PathEffects
from matplotlib.legend_handler import HandlerBase
import matplotlib


parser = ap.ArgumentParser()
parser.add_argument('spec')
parser.add_argument('-o', dest = 'out', required = True)
parser.add_argument('-d', dest = 'dir', required = True)
parser.add_argument('-n', '--naked', action = 'store_true')
parser.add_argument('-i', '--index', type = int, required = False, default = -1)
parser.add_argument('--positive', action = 'store_true')
# parser.add_argument('--ymax', type = int, default = None)
# parser.add_argument('--ymin', type = int, default = None)
args = parser.parse_args()


with open(args.spec) as f:
    spec = SimpleNamespace(**json.load(f))

width = 0.5
fig, ax = plt.subplots()
bottom = np.zeros(len(spec.benchmarks))

plt.rcParams['hatch.linewidth'] = spec.hatchweight
# plt.rcParams['text.usetex'] = True
# plt.rc('font', size = spec.fontsize, family = 'Times New Roman') #, weight = 'bold')
plt.rcParams.update({
    "text.usetex": True,
    "font.family": "serif",
    "font.serif": ["Times", "Palatino", "serif"],
    "font.size": spec.fontsize
})

# Add data for each benchmark at a time

def get_benchmark_id(bench):
    return f'{bench.lib}_{bench.name}_{bench.size}'

bench_disp_counter = 0

bench_disp = dict()

def get_benchmark_disp(bench):
    # convert size to bytes
    if bench.size >= 1024:
        size = f'{int(bench.size / 1024)}KB'
    else:
        size = f'{int(bench.size)}B'
    
    return f'{bench.lib}\n{bench.name}\n{size}'
    global bench_disp
    t = (bench.lib, bench.name, bench.size)
    if t in bench_disp:
        return bench_disp[t]
    if len(bench_disp) % 2:
        s = f'{bench.lib} {bench.name}\n({bench.size})'
    else:
        s = f'{bench.lib}\n{bench.name} ({bench.size})'
    bench_disp[t] = s
    return s
    # return f'{bench.lib}\n{bench.name}\n({bench.size})'

def load_result(bench, component):
    benchid = get_benchmark_id(bench)
    path = f'{args.dir}/time_{benchid}_{component}.json'
    with open(path) as f:
        j = json.load(f)
    for e in j['benchmarks']:
        key = 'aggregate_name'
        if key in e and e[key] == 'mean':
            return float(e['cpu_time'])
    assert False

def load_geomean_overhead(component, key):
    l = []
    for bench in spec.benchmarks:
        bench = SimpleNamespace(**bench)
        if key == 'geomean-large' and int(bench.size) < 1024:
            continue
        v = load_overhead(bench, component)
        l.append(v)
    l = [v / 100 + 1 for v in l]
    mean = pow(math.prod(l), 1.0 / len(l))
    res = (mean - 1) * 100
    print(key, component, res)
    return res

def load_overhead(bench, component):
    if bench == 'geomean' or bench == 'geomean-large':
        return load_geomean_overhead(component, bench)
    base = load_result(bench, 'base')
    comp = load_result(bench, component)
    res = (comp / base - 1) * 100
    print(bench.lib, bench.name, bench.size, component, res)
    return res

data = defaultdict(list)
agg = defaultdict(list)
agg_large = defaultdict(list)

def add_record_raw(benchid, mitigation, overhead, md):
    data['benchmark'].append(benchid)
    data['mitigation'].append(mitigation)
    data['overhead'].append(overhead)
    data['md'].append(md)

def add_record(bench, mitigation, overhead, md):
    add_record_raw(get_benchmark_disp(bench), mitigation, overhead, md)
    agg[mitigation].append(overhead)
    if int(bench.size) >= 1024:
        agg_large[mitigation].append(overhead)


def add_bar(bench, mitigation):
    # bench = SimpleNamespace(**bench)
    mitigation = SimpleNamespace(**mitigation)
    if args.index >= len(mitigation.components):
        overhead = 0
    else:
        overhead = load_overhead(bench, mitigation.components[args.index])
    add_record(bench, mitigation.name, overhead, md = (bench, mitigation))

def add_benchmark(bench):
    bench = SimpleNamespace(**bench)
    for mitigation in spec.mitigations:
        add_bar(bench, mitigation)

for bench in spec.benchmarks:
    add_benchmark(bench)

# compute the geomean for each mitigation
def add_geomean(agg, name, key):
    for mitigation, overheads in agg.items():
        prod = math.prod([x / 100 + 1 for x in overheads])
        mean = pow(prod, 1.0 / len(overheads))
        mean = (mean - 1) * 100
        # ugh, reverse lookup mitigation
        themit = None
        for mit in spec.mitigations:
            mit = SimpleNamespace(**mit)
            if mit.name == mitigation:
                themit = mit
        add_record_raw(name, mitigation, mean, md = (key, themit))
        # data['benchmark'].append(name)
        # data['mitigation'].append(mitigation)
        # data['overhead'].append(mean)

add_geomean(agg, 'geomean\n(all)', 'geomean')
add_geomean(agg_large, 'geomean\n(8KB)', 'geomean-large')

if args.positive:
    todo = []
    for i, overhead in enumerate(data['overhead']):
        if overhead < 0:
            todo.append(i)
    for i in todo:
        for key, l in data.items():
            del l[i]

# plt.margins(x=0,y=0)
# plt.rc('axes', xmargin = 0, ymargin = 0)
df = pd.DataFrame(data = data)
g = sns.catplot(
    data = df,
    kind = 'bar',
    x = 'benchmark',
    y = 'overhead',
    hue = 'mitigation',
    legend = None,
    height = spec.height,
    aspect = spec.width / spec.height,
    alpha = spec.alpha
)
ax = g.facet_axis(0, 0)
plt.tight_layout()
# plt.subplots_adjust(bottom = 0.1)

if spec.ymax:
    ax.set_ybound(upper = spec.ymax)
if spec.ymin:
    ax.set_ybound(lower = spec.ymin)

# label bars
for c in ax.containers:
    labels = []
    todos = []
    for v in c:
        val = v.get_height()
        s = f'{val:.1f}'
        labels.append(s)
        v.set_edgecolor('black')
        v.set_linewidth(spec.weight)
        # v.set_hatch('////')
        
        # ax.add_patch(Rectangle(v.xy, v.get_width(), v.get_height() / 2))

    if not args.naked:
        pass # texts = ax.bar_label(c, labels = labels, label_type = 'edge', rotation = 90, fontsize = 'small')

inv = ax.transData.inverted()
        
# now add in partial mitigations
def add_partial_result(rect, bench, mitigation):
    components = zip(mitigation.components, mitigation.labels, mitigation.hatch)
    prev = 0
    last = load_overhead(bench, mitigation.components[-1])
    for component, label, hatch in components:
        overhead = load_overhead(bench, component)
        overhead = min(overhead, last)
        if overhead < prev or prev >= spec.ymax:
            continue
        hatchkw = {}
        if spec.shouldhatch:
            hatchkw['hatch'] = hatch * spec.hatchdensity
        extra = Rectangle((rect.get_x(), rect.get_y() + prev), rect.get_width(), overhead - prev,
                          facecolor = rect.get_facecolor(), edgecolor = rect.get_edgecolor(),
                          linewidth = rect.get_linewidth(), **hatchkw)
        if extra.get_y() < spec.ymax and extra.get_y() + extra.get_height() > spec.ymax:
            extra.set_height(spec.ymax - extra.get_y())
        # ax.add_patch(Rectangle(rect.xy, rect.get_width(), overhead, color = rect.get_facecolor()))
        ax.add_patch(extra)


        # place label for now
        tmpa = ax.transData.transform(extra.xy)
        tmpb = ax.transData.transform((extra.get_x() + extra.get_width(), extra.get_y() + extra.get_height()))
        w = tmpb[0] - tmpa[0]
        h = tmpb[1] - tmpa[1]
        rotation = 0 if w > h and False else 90
        t = plt.text(*extra.get_center(), label, rotation = rotation,
                     horizontalalignment = 'center', verticalalignment = 'center',
                     color = 'black',
                     fontsize = spec.component_fontsize,
                     )

        
        # t.set_path_effects([PathEffects.withStroke(linewidth = 0.2, foreground = 'black')])
        bb = t.get_window_extent(renderer = ax.get_figure().canvas.get_renderer())
        # if inv(bb.height) > extra.get_height() * 3:
        # bb = inv.transform(bb)
        # if bb[1][1] - bb[0][1] > h or bb[1][0] - bb[0][0] > w:

        if extra.get_height() < spec.component_threshold:
            t.set_visible(False)
        
        if bb.height > h or bb.width > w:
            # t.set_visible(False)
            pass

        bb2 = inv.transform(bb)
        if 2 * (bb2[1][1] - bb2[0][1]) > spec.ymax - bb2[1][1]:
            # t.set_visible(False)
            pass
        
        prev = overhead

for c in ax.containers:
    for bar in c:
        h = bar.get_height()
        # we need to find a bar of the same height
        for i, overhead in enumerate(data['overhead']):
            if overhead == h:
                bench, mitigation = data['md'][i]
                if bench == 'geomean' and False:
                    continue
                add_partial_result(bar, bench, mitigation)

        kwargs = {}
        if h >= spec.bar_label_threshold and False:
            kwargs = {
                'color': 'white',
                'verticalalignment': 'top'
            }
            dir = -1
        else:
            kwargs = {
                'color': 'black',
                'verticalalignment': 'bottom'
            }
            dir = 1
            
            
        plt.text(bar.get_x() + bar.get_width() / 2, min(spec.ymax, h) + dir * 1,
                 f'{h:.1f}', rotation = 90, horizontalalignment = 'center', 
                 fontsize = spec.barlabel_fontsize,
                 **kwargs)
        # color = 'white', fontsize = 'small', rotation = 90,
        # horizontalalignment = 'center', verticalalignment = 'top')


ax.set_xlabel(None)
ax.set_ylabel('overhead (\\%)', labelpad = 0.75, fontsize = spec.labelfontsize)
            
if args.naked:
    ax.set_frame_on(False)
    ax.set_xlabel(None)
    ax.set_ylabel(None)
    ax.set_xticks([])
    ax.set_yticks([])

ax.tick_params(labelsize = spec.labelfontsize, pad = 1)


handles, labels = ax.get_legend_handles_labels()
r = matplotlib.patches.Rectangle((0,0), 1, 1, fill=False, edgecolor='none',
                                 visible=False)
handles.insert(2, r)
labels.insert(2, '')

legend = ax.legend(
    handles, labels,
    loc = 'upper left',
    labelspacing = 0.1,
    bbox_to_anchor = tuple(spec.legendloc),
    handlelength = 1.5,
    handletextpad = 0.5,
    fontsize = spec.labelfontsize,
    borderpad = 0.2,
    ncols = 2,
    columnspacing = 0.75,
)
for patch in legend.get_patches():
    patch.set_alpha(pow(spec.alpha, 0.5))

plt.savefig(f'{args.out}', transparent = True, pad_inches = 0, bbox_inches = 'tight')
