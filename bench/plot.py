import argparse
import sys
import os
import json
import tempfile
import math
import matplotlib.pyplot as plt
import seaborn
import shutil
import collections
import pandas

parser = argparse.ArgumentParser()
parser.add_argument('spec')
parser.add_argument('-o', dest = 'outdir', required = True, action = 'store')
parser.add_argument('-a', dest = 'run_all', action = 'store_true')
parser.add_argument('-B', dest = 'remake_all', action = 'store_true')
args = parser.parse_args()

with open(args.spec) as f:
    spec = json.load(f)

if args.remake_all:
    shutil.rmtree(args.outdir)

os.makedirs(args.outdir, exist_ok = True)
    
benchmark_flags = spec['benchmark_flags']
def run_benchmark(exe, outpath):
    args = [exe, f'--benchmark_out={outpath}', '--benchmark_out_format=json', *benchmark_flags]
    cmd = ' '.join(args)
    print(f'Running: {cmd}', file = sys.stderr)
    if os.system(' '.join(args)) != 0:
        print('benchmark failed: ', args, file = sys.stderr)
        exit(1)


def maybe_run_benchmark(exe, outpath):
    if args.run_all or not os.access(outpath, os.R_OK):
        run_benchmark(exe, outpath)
        assert os.access(outpath, os.R_OK)

# Required format:
# x = benchmark name
# y = overhead
# hue = mitigation
# errorbar = sd

# benches: benchname -> mitname -> recordname -> values
benches = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(list)));

for benchname, benchspec in spec['benchmarks'].items():
    print(f'Running tests for {benchname}', file = sys.stderr)
    for mitigation_name, mitigation_exe in benchspec.items():
        print(f'Running for mitigation {mitigation_name}', file = sys.stderr)
        outpath = f'{args.outdir}/{benchname}_{mitigation_name}.json'
        maybe_run_benchmark(mitigation_exe, outpath)
        with open(outpath, 'r') as f:
            print(f'Parsing {outpath}', file = sys.stderr)
            j = json.load(f)
        # process json
        for d in j['benchmarks']:
            benches[benchname][mitigation_name][d['name']].append(d['cpu_time'])

data = collections.defaultdict(list)

geomean_in = collections.defaultdict(list)

for plotspec in spec['plots']:
    benchname = plotspec['name']

    # get json results
    ref = None
    for mitigation in spec['mitigations']:
        mitname = mitigation['name']
        mitdisplay = mitigation['displayname']
        j = benches[benchname][mitname]
        key = plotspec['key']
        if 'reference' in mitigation:
            ref = j[f'{key}_mean'][0]
            continue
        means = j[f'{key}_mean']
        for mean in means:
            mean_pct = (mean / ref - 1) * 100
            data['benchmark'].append(plotspec['displayname'])
            data['mitigation'].append(mitdisplay)
            data['overhead'].append(mean_pct)
            geomean_in[mitname].append(mean_pct)


for mitigation in spec['mitigations']:
    if 'reference' in mitigation:
        continue
    mitname = mitigation['name']
    mitdisplay = mitigation['displayname']
    l = geomean_in[mitname]
    l = [d / 100 + 1 for d in l]
    assert math.prod(l) > 0
    geomean = (math.prod(l) ** (1.0 / len(l)) - 1) * 100
    data['benchmark'].append('Geometric\nmean')
    data['mitigation'].append(mitdisplay)
    data['overhead'].append(geomean)

df = pandas.DataFrame(data = data)
g = seaborn.catplot(
    data = df,
    kind = 'bar',
    x = 'benchmark',
    y = 'overhead',
    hue = 'mitigation',
    errorbar = 'sd',
)
# ax.set_xticklabels(rotation = 46, horizontalalignment = 'right', fontsize = 7.5)
g.set_xticklabels(fontsize = 6)
ax = g.facet_axis(0, 0)
# ax.figure.tight_layout()

for c in ax.containers:
    labels = [f'{v.get_height():.1f}' for v in c]
    ax.bar_label(c, labels = labels, label_type = 'edge', rotation = 0, fontsize = 5)

plt.legend(
    fontsize = '7.5',
    loc = 'upper left',
    bbox_to_anchor = (0.07, 0.9)
)

g.set(
    ylabel = 'overhead (%)'
)

plt.savefig(f'{args.outdir}/plot.pdf')
