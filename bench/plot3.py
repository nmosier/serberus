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

parser = ap.ArgumentParser()
parser.add_argument('spec')
parser.add_argument('-o', dest = 'out', required = True)
parser.add_argument('-d', dest = 'dir', required = True)
parser.add_argument('-n', '--naked', action = 'store_true')
parser.add_argument('-i', '--index', type = int, required = False, default = -1)
parser.add_argument('--positive', action = 'store_true')
parser.add_argument('--ymax', type = int, default = None)
parser.add_argument('--ymin', type = int, default = None)
args = parser.parse_args()


with open(args.spec) as f:
    spec = SimpleNamespace(**json.load(f))

width = 0.5
fig, ax = plt.subplots()
bottom = np.zeros(len(spec.benchmarks))

# Add data for each benchmark at a time

def get_benchmark_id(bench):
    return f'{bench.lib}_{bench.name}_{bench.size}'

def get_benchmark_disp(bench):
    return f'{bench.lib}\n{bench.name}\n{bench.size}'

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
    return (mean - 1) * 100

def load_overhead(bench, component):
    if bench == 'geomean' or bench == 'geomean-large':
        return load_geomean_overhead(component, bench)
    base = load_result(bench, 'base')
    comp = load_result(bench, component)
    return (comp / base - 1) * 100

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
add_geomean(agg_large, 'geomean\n($\geq$1KB)', 'geomean-large')

if args.positive:
    todo = []
    for i, overhead in enumerate(data['overhead']):
        if overhead < 0:
            todo.append(i)
    for i in todo:
        for key, l in data.items():
            del l[i]

df = pd.DataFrame(data = data)
aspect = 3
g = sns.catplot(
    data = df,
    kind = 'bar',
    x = 'benchmark',
    y = 'overhead',
    hue = 'mitigation',
    legend = None,
    aspect = aspect,
)
ax = g.facet_axis(0, 0)

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
        # v.set_hatch('////')
        
        # ax.add_patch(Rectangle(v.xy, v.get_width(), v.get_height() / 2))

    if not args.naked:
        texts = ax.bar_label(c, labels = labels, label_type = 'edge', rotation = 90, fontsize = 'small')

inv = ax.transData.inverted()
        
# now add in partial mitigations
def add_partial_result(rect, bench, mitigation):
    components = zip(mitigation.components, mitigation.labels, mitigation.hatch)
    prev = 0
    last = load_overhead(bench, mitigation.components[-1])
    for component, label, hatch in components:
        overhead = load_overhead(bench, component)
        overhead = min(overhead, last)
        if overhead < prev:
            continue
        extra = Rectangle((rect.get_x(), rect.get_y() + prev), rect.get_width(), overhead - prev,
                          facecolor = rect.get_facecolor(), edgecolor = rect.get_edgecolor())
        if extra.get_y() < spec.ymax and extra.get_y() + extra.get_height() > spec.ymax:
            extra.set_height(spec.ymax - extra.get_y())
        # ax.add_patch(Rectangle(rect.xy, rect.get_width(), overhead, color = rect.get_facecolor()))
        ax.add_patch(extra)
        

        # place label for now
        tmpa = ax.transData.transform(extra.xy)
        tmpb = ax.transData.transform((extra.get_x() + extra.get_width(), extra.get_y() + extra.get_height()))
        w = tmpb[0] - tmpa[0]
        h = tmpb[1] - tmpa[1]
        rotation = 0 if w > h else 90
        t = plt.text(*extra.get_center(), label, rotation = rotation,
                     horizontalalignment = 'center', verticalalignment = 'center', fontsize = 'small', color = 'white')
        bb = t.get_window_extent(renderer = ax.get_figure().canvas.get_renderer())
        # if inv(bb.height) > extra.get_height() * 3:
        # bb = inv.transform(bb)
        # if bb[1][1] - bb[0][1] > h or bb[1][0] - bb[0][0] > w:
        
        if bb.height > h or bb.width > w:
            t.set_visible(False)

        bb2 = inv.transform(bb)
        print(bb2)
        if 2 * (bb2[1][1] - bb2[0][1]) > spec.ymax - bb2[1][1]:
            t.set_visible(False)
        
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
        if h >= spec.ymax:
            plt.text(bar.get_x() + bar.get_width() / 2, spec.ymax,
                     f'{h:.1f}', color = 'white', fontsize = 'small', rotation = 90,
                     horizontalalignment = 'center', verticalalignment = 'top')

if args.naked:
    ax.set_frame_on(False)
    ax.set_xlabel(None)
    ax.set_ylabel(None)
    ax.set_xticks([])
    ax.set_yticks([])

plt.legend()

plt.savefig(f'{args.out}', transparent = True)
