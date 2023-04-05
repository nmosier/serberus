import argparse
import shutil
import os
from collections import defaultdict
import json
import pandas
import seaborn
import matplotlib.pyplot as plt
import math

parser = argparse.ArgumentParser()
parser.add_argument('json', nargs = '+')
parser.add_argument('-o', dest = 'out')
args = parser.parse_args()

def get_basename(path):
    return os.path.splitext(os.path.basename(path))[0]

# Infer the metric from the filenames

metric = get_basename(args.json[0]).split('_', maxsplit = 1)[0]
metric_keys = {'time': 'cpu_time'}
metric_key = metric_keys[metric] if metric in metric_keys else metric

# From the JSON filenames, we can gather the exact benchmarks.
# time_<lib>_<size>_<mode>.json

# function_displaynames = {
#     "salsa20": "Salsa20",
#     "sha256": "SHA-256",
#     "chacha20": "ChaCha20",
#     "poly1305": "Poly1305",
#     "curve25519": "Curve25519",
# }


# The structure needs to looks like the following.
# | <name>_<size> | overhead | mitigation |

# map from run (<lib>_<name>_<size>) to list of baseline types
benchmarks = defaultdict(dict)
for jsonpath in args.json:
    benchtype, lib, name, size, mode = os.path.basename(os.path.splitext(jsonpath)[0]).split('_', maxsplit = 4)

    benchmark = (lib, name, size)
    with open(jsonpath, 'r') as f:
        j = json.load(f)
    # strip away unneeded information in json
    j_ = None
    for result in j['benchmarks']:
        if result['name'] == f'{lib}_{name}/{size}_mean':
            j_ = result
            break
    assert j_ != None
    benchmarks[benchmark][mode] = j_

# assemble results into table
data = {
    'benchmark': [],
    'overhead': [],
    'mitigation': [],
}

geomean_in = defaultdict(list)

def mitigation_displayname(mitigation):
    return mitigation.split('_', maxsplit = 1)[-1]

def benchmark_displayname(lib, name, size):
    return f'{lib}\n{name}\n({size})'

# TODO: have option for absolute-number style barcharts, not just overhead-style.
is_absolute = (metric == 'mitigation')

for benchmark in benchmarks:
    results = benchmarks[benchmark]
    baseline = results['baseline_none']
    baseline_metric = baseline[metric_key]
    for mitigation, result in results.items():
        if mitigation == 'baseline_none':
            continue
        mitigation_metric = result[metric_key]

        if is_absolute:
            overhead = mitigation_metric
        elif baseline_metric == 0:
            overhead = 0
        else:
            overhead = (mitigation_metric - baseline_metric) / baseline_metric * 100
        data['benchmark'].append(benchmark_displayname(*benchmark))
        data['overhead'].append(overhead)
        data['mitigation'].append(mitigation_displayname(mitigation))

        geomean_in[mitigation].append(overhead)

scores = {}
        
for mitigation, l in geomean_in.items():
    data['benchmark'].append('geomean')
    l1 = [x / 100 + 1 for x in l]
    x = math.prod(l1) ** (1 / len(l))
    y = (x - 1) * 100
    data['overhead'].append(y)
    data['mitigation'].append(mitigation_displayname(mitigation))
    scores[mitigation_displayname(mitigation)] = y

    data['benchmark'].append('arithmean')
    data['overhead'].append(sum(l) / len(l))
    data['mitigation'].append(mitigation_displayname(mitigation))


aspect = 2
    
df = pandas.DataFrame(data = data)
g = seaborn.catplot(
    data = df,
    kind = 'bar',
    x = 'benchmark',
    y = 'overhead',
    hue = 'mitigation',
    legend = None,
    aspect = aspect,
)

g.set_xticklabels(rotation = 45, horizontalalignment = 'center', fontsize = 'small')
plt.tight_layout()

ax = g.facet_axis(0, 0)

# FIXME: make it proportional, not fixed
# REVERTME
# ymax = 1500
ymax = None
if ymax is not None:
    if ax.get_ybound()[1] >= ymax: 
        ax.set_ybound(upper = ymax)

# fix up bars
for c in ax.containers:
    labels = []
    for v in c:
        val = v.get_height()
        if int(val) == val:
            s = str(int(val))
        else:
            s = f'{val:.1f}'
        labels.append(s)
        if ymax is not None:
            v.set_height(min(v.get_height(), ymax))
    texts = ax.bar_label(c, labels = labels, label_type = 'edge', rotation = 90, fontsize = 'small')

    
plt.legend(
#    loc = 'upper center',
)

if args.out:
    plt.savefig(f'{args.out}')


# also write file
with open(args.out + '.txt', 'w') as f:
    for mitigation, score in scores.items():
        print(f'{mitigation} {score:.2f}', file = f)
