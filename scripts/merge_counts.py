import argparse
import sys
import collections

parser = argparse.ArgumentParser(description = 'Merge Counts in Table')
parser.add_argument('-t', type = int, required = True, dest = 'column')
args = parser.parse_args()

col = args.column - 1

# map tuples missing column
d = collections.defaultdict(int)

for line in sys.stdin:
    tokens = line.split()
    assert len(tokens) > col
    count = int(tokens[col])
    tokens = tokens[:col] + tokens[col+1:]
    tokens = tuple(tokens)
    d[tokens] += count

for tokens in d:
    tokens = list(tokens[:col]) + [str(d[tokens])] + list(tokens[col:])
    print(' '.join(tokens))
