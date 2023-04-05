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

def load_overhead(bench, component):
    base = load_result(bench, 'base')
    comp = load_result(bench, component)
    return (comp / base - 1) * 100

data = defaultdict(list)
agg = defaultdict(list)
agg_large = defaultdict(list)

def add_record_raw(benchid, mitigation, overhead):
    data['benchmark'].append(benchid)
    data['mitigation'].append(mitigation)
    data['overhead'].append(overhead)

def add_record(bench, mitigation, overhead):
    add_record_raw(get_benchmark_disp(bench), mitigation, overhead)
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
    add_record(bench, mitigation.name, overhead)

def add_benchmark(bench):
    bench = SimpleNamespace(**bench)
    for mitigation in spec.mitigations:
        add_bar(bench, mitigation)

for bench in spec.benchmarks:
    add_benchmark(bench)

# compute the geomean for each mitigation
def add_geomean(agg, name):
    for mitigation, overheads in agg.items():
        prod = math.prod([x / 100 + 1 for x in overheads])
        mean = pow(prod, 1.0 / len(overheads))
        mean = (mean - 1) * 100
        data['benchmark'].append(name)
        data['mitigation'].append(mitigation)
        data['overhead'].append(mean)

add_geomean(agg, 'geomean\n(all)')
add_geomean(agg_large, 'geomean\n($\geq$1KB)')

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

if args.ymax:
    ax.set_ybound(upper = args.ymax)
if args.ymin:
    ax.set_ybound(lower = args.ymin)

# label bars
for c in ax.containers:
    labels = []
    todos = []
    for v in c:
        val = v.get_height()
        s = f'{val:.1f}'
        labels.append(s)
        v.set_edgecolor('black')
        v.set_hatch('////')
        
        # ax.add_patch(Rectangle(v.xy, v.get_width(), v.get_height() / 2))

    if not args.naked:
        texts = ax.bar_label(c, labels = labels, label_type = 'edge', rotation = 90, fontsize = 'small')


# now add in partial mitigations


if args.naked:
    ax.set_frame_on(False)
    ax.set_xlabel(None)
    ax.set_ylabel(None)
    ax.set_xticks([])
    ax.set_yticks([])

plt.savefig(f'{args.out}', transparent = True)
