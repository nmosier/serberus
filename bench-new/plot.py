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
metric_keys = {
    'time': 'cpu_time',
    'mem': 'mem',
    'cache': 'cache',
}
metric_key = metric_keys[metric]

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

for benchmark in benchmarks:
    results = benchmarks[benchmark]
    baseline = results['baseline_none']
    baseline_metric = baseline[metric_key]
    for mitigation, result in results.items():
        if mitigation == 'baseline_none':
            continue
        mitigation_metric = result[metric_key]
        overhead = (mitigation_metric - baseline_metric) / baseline_metric * 100
        data['benchmark'].append(benchmark_displayname(*benchmark))
        data['overhead'].append(overhead)
        data['mitigation'].append(mitigation_displayname(mitigation))

        geomean_in[mitigation].append(overhead)

# TODO: add the geometric mean
for mitigation, l in geomean_in.items():
    data['benchmark'].append('geomean')
    l1 = [x / 100 + 1 for x in l]
    x = math.prod(l1) ** (1 / len(l))
    y = (x - 1) * 100
    data['overhead'].append(y)
    data['mitigation'].append(mitigation_displayname(mitigation))

df = pandas.DataFrame(data = data)
g = seaborn.catplot(
    data = df,
    kind = 'bar',
    x = 'benchmark',
    y = 'overhead',
    hue = 'mitigation',
)

g.set_xticklabels(rotation = 45, horizontalalignment = 'center')
plt.tight_layout()

if args.out:
    plt.savefig(f'{args.out}')
