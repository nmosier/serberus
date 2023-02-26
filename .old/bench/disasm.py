import argparse
import sys
import os
import json
import collections

parser = argparse.ArgumentParser()
parser.add_argument('--json', required = True)
parser.add_argument('--bin', required = True)
parser.add_argument('-o', required = True, dest = 'out')
args = parser.parse_args()

with open(args.json) as f:
    j = json.load(f)

functions = j['functions']

if os.path.exists(args.out):
    os.remove(args.out)

for function in functions:
    os.system(f'gdb -batch -ex \"disassemble {function}\" {args.bin} >> {args.out}')
