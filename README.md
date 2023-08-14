# LLSCT

## REVIEWERS: 
This repository contains some of the passes implementing LLSCT, but not all.
The rest of LLSCT is implemented here: [llsct-llvm](https://anonymous.4open.science/r/sctcc).

[bench/additional-baselines.md](bench/additional-baselines.md) contains the full results including the additional baseline mitigations based on BladeSLH and UltimateSLH, which we did not have space to include in the paper.

## Building
LLSCT is only supported for Linux (but may run on Intel-based Macs with some tweaks).

Requires gcc-12 (for C++20 features).

### Dependencies
LLSCT currently requires the following dependencies:
- gperftools
- libunwind
- CMake (version >= 3.25)
- Ninja
- Python3
- Python packages: pandas, seaborn
- GCC 12
You can install all of these using [Homebrew ](https://brew.sh).

### Installing Dependencies
Here's how to install LLSCT's dependencies using Homebrew.
```sh
brew install gperftools libunwind cmake ninja python3 gcc binutils glibc
pip3 install pandas seaborn
export LD_LIBRARY_PATH="$(brew --prefix gcc)/lib/gcc/current:$LD_LIBRARY_PATH"
```

### Building llsct-llvm
To build LLSCT, you will need to clone two repositories: [llsct-llvm](https://github.com/nmosier/clouxx-llvm) and llsct-passes (this repository).
First, clone and build __llsct-llvm__:
```sh
git clone https://github.com/nmosier/clouxx-llvm --depth=1 llsct-llvm
mkdir llsct-llvm/build && cd llsct-llvm/build
cmake -G Ninja -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_COMPILER=g++-12 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -DLLVM_ENABLE_ASSERTIONS=On -DLLVM_ENABLE_PROJECTS='clang;lld' -DLLVM_TARGETS_TO_BUILD='X86' ../llvm
ninja
ninja install
cd ../..
```

### Building llsct-passes
Now, clone and configure __llsct-passes__:
```sh
git clone https://github.com/nmosier/clouxx-passes llsct-passes
mkdir llsct-passes/build && cd llsct-passes/build
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++-12 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLSCT_LLVM_DIR=$PWD/../../llsct-llvm/install -DLLSCT_REQUIRE_CET=Off ..
ninja src/all
```
The last command builds all of LLSCT's IR passes.
The `-DLLSCT_ENABLE_CET=Off` flag disables runtime Intel CET enforcement if your Linux distribution doesn't support userspace CET (at the time of writing, none of them do).

To build all the benchmark programs so that you can run them as standaloen programs, build the `raw_compile` target:
```sh
ninja raw_compile
```
All the test binaries will have filenames matching `bench-new/raw_<project>_<name>_<size>_<mitigation>`.

To run the benchmark to obtain a graph of runtime overhead:
```sh
ninja time_compile clean_bench && ninja -j1 time_pdf
```
and a PDF of the overhead plot will be written to `bench-new/time.pdf`.

### Troubleshooting
```sh
sudo cpupower frequency-set --governor performance
```

### Citing LLSCT
If you use LLSCT in your work, we would appreciate it if you cite our paper ([bibtex](/cite.bib)):
> N. Mosier, H. Nemati, J. Mitchell, C. Trippel, "Serberus: Protecting Cryptographic Code from Spectres at Compile-Time," _2024 IEEE Symposium on Security and Privacy (S&P)_, 2024.
