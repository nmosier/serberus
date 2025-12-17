# Serberus (i.e., the LLSCT compiler)

This is the repository for Serberus, a software Spectre defense presented at S&P'24.
Serberus secures (static) constant-time programs against:
- Spectre-PHT: conditional branch mispredictions
- Spectre-BTB: indirect branch misprediction (includes Spectre-BHB, etc.)
- Spectre-RSB: return address misprediction
- 
on recent Intel E-cores out-of-the-box. Due to P-cores' limited (2 instructions, 1 load)
but nonzero speculation window following a missing `ENDBRANCH` under Intel CET-IBT, P-cores require an additional
binary rewriting pass (e.g., using LLVM Bolt) to detect <= 2-instruction, <= 1-load gadgets and break them up with
LFENCEs (such gadgets are rare, due to the length constraints).

[bench/additional-baselines.md](bench/additional-baselines.md) contains the full results including the additional baseline mitigations based on BladeSLH and UltimateSLH, which we did not have space to include in the paper.

## Overview
Serberus consists of two pieces -- in-tree modifications to LLVM ([llvm/](llvm/)) and some out-of-tree passes ([src/](src/)).

## Building
LLSCT is only supported for Linux (but may run on Intel-based Macs with some tweaks).

Requires gcc-12+ (for C++20 features).

### Cloning
Clone this repository as follows:
```sh
git clone https://github.com/nmosier/serberus.git
cd serberus
git submodule update --init --recursive
```
This may take a while, since it will clone Serberus' fork of LLVM as a submodule.
You can add the option `--depth=1` to shallow-clone LLVM, which should be faster
and take up less disk space.

### Installing Dependencies
You can either launch a Docker container with all dependencies installed ([Docker](#Docker))
or install the dependencies on the host ([Host](#Host))

#### Docker
Now that Serberus is cloned, 
you can use Docker to automatically install all dependencies necessary for building Serberus and running experiments.
We provide a script for building the Docker image ([docker/build.sh](docker/build.sh)) and 
starting a Docker container ([docker/run.sh](docker/run.sh)).
This Docker container mounts the Serberus source directory inside the container at runtime, so you will be able 
to inspect outputs from the host.

#### Host
Serberus requires the following dependencies to build and run the experiments described later in the README:
```sh
apt-get install -y libgoogle-perftools-dev google-perftools libunwind-dev cmake ninja-build build-essential python3 python-is-python3 git pkg-config python3-venv sudo
pip install pandas seaborn
```
You can omit some of these if you only want to build Serberus, but not run the paper experiments.

### Building LLVM
To build Serberus' fork of LLVM, run the following script:
```sh
./build-llvm.sh
```
This may take a while, since LLVM is a large project.

### Building Out-of-Tree Passes
To build Serberus' out-of-tree passes, run the following script:
```sh
./build-passes.sh
```

### Building Benchmarks
To build the benchmarks that we evaluate in the paper, run:
```sh
./build-bench.sh
```
This will build all benchmark programs in two ways:
- `bench/raw_<project>_<name>_<size>_<defense>`: Standalone benchmarks that you can run directly.
- `bench/time_<project>_<name>_<size>_<defense>`: Benchmarks compiled with googlebench to benchmark runtime.

## Reproducing Paper Results
To run the paper benchmarks to obtain a graph of runtime overhead:
```sh
ninja -j1 time_pdf
```
and a PDF of the overhead plot will be written to `bench/time.pdf`.

### Troubleshooting
If googlebench is complaining about inaccurate measurements due to frequency scaling being enabled, you can disable it with:
```sh
sudo cpupower frequency-set --governor performance
```

### Citing Serberus
If you use Serberus in your work, we would appreciate it if you cite our paper ([bibtex](/cite.bib)):
> N. Mosier, H. Nemati, J. Mitchell, C. Trippel, "Serberus: Protecting Cryptographic Code from Spectres at Compile-Time," _2024 IEEE Symposium on Security and Privacy (S&P)_, 2024.
