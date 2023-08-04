# Additional State-of-the-Art Baselines
In the Serberus paper, we evaluate only two partially secure state-of-the-art baselines, 
lfence+retpoline+ssbd (a.k.a. f+retp+ssbd) and slh+retpoline+ssbd (a.k.a. s+retp+ssbd).
Here, we report results for two additional state-of-the-art baselines.

Our first new baseline is bladeslh+retpoline+ssbd, a composite mitigation which uses [our LLVM implementation](https://github.com/nmosier/clouxx-llvm/blob/c62bc72c761036036753cdeff9ab63ee58e6f528/llvm/lib/Target/X86/X86SpeculativeLoadHardening.cpp#L1005) of [BladeSLH](https://dl.acm.org/doi/pdf/10.1145/3434330)
(which originally targets WebAssembly).
Our second new baseline is uslh+retpoline+ssbd, a composite mitigation which directly uses the published implementation of [UltimateSLH](https://www.usenix.org/system/files/sec23fall-prepub-278-zhang-zhiyuan.pdf) for LLVM.
Our implementation of BladeSLH is more optimzed than the published version;
specifically, we harden CA stack loads using only one conditional move (into the stack pointer) at the beginning of each basic block that is the target of a conditional branch
(instead of hardening CA stack loads individually).
In implementing BladeSLH, we observe it is vulnerable to PHT [SpecROP](https://www.usenix.org/system/files/raid20-bhattacharyya.pdf)-style return address overwrites;
to render it a complete PHT mitigation, we insert a `LFENCE` before each return.
bladeslh+retpoline+ssbd and uslh+retpoline+ssbd exhibit 32.6%/11.4% and 21.1%/9.2% overhead, respectively, for all/large-buffer benchmarks.
Both have comparabdle or worse performance than Serberus/LLSCT with weaker security guarantees (e.g., they do not protect against Spectre-RSB).

