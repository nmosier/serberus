
#include <ctime>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <variant>
#include <iomanip>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Clou/Clou.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>

#include <err.h>

#include "clou/MinCutSMT.h"
#include "clou/MinCutGreedy.h"
#include "clou/util.h"
#include "clou/Transmitter.h"
#include "clou/CommandLine.h"
#include "clou/Mitigation.h"
#include "clou/Log.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/ConstantAddressAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/Stat.h"
#include "clou/containers.h"
#include "clou/CFG.h"

namespace clou {
  namespace {

    static std::ofstream openFile(const llvm::Function& F, const char *suffix) {
      std::string path;
      llvm::raw_string_ostream path_os(path);
      path_os << ClouLogDir << "/";
      {
	char *source_path = ::strdup(F.getParent()->getSourceFileName().c_str());
	char *source_file = ::basename(source_path);
	path_os << source_file;
	std::free(source_path);
      }
      if (::mkdir(path.c_str(), 0770) < 0 && errno != EEXIST)
	err(EXIT_FAILURE, "mkdir: %s", path.c_str());
      path_os << "/" << F.getName() << suffix;
      return std::ofstream(path);
    }

    extern "C" void ProfilerStart(const char *);
    extern "C" void ProfilerStop(void);
    static bool profile_active = false;
    static struct sigaction oldact;
    __attribute__((constructor)) static void profile_start(void) {
      if (const char *s = std::getenv("PROFDIR")) {
	char *path;
	const char *suffix = ".prof";
	if (asprintf(&path, "%s/XXXXXX%s", s, suffix) < 0)
	  err(EXIT_FAILURE, "asprintf");
	const int fd = mkstemps(path, std::strlen(suffix));
	if (fd < 0)
	  err(EXIT_FAILURE, "mkstemp: %s", path);
	close(fd);	
	ProfilerStart(path);
	std::free(path);
	profile_active = true;
      } else if (const char *s = std::getenv("PROF")) {
	ProfilerStart(s);
	profile_active = true;
      }

      if (profile_active) {
	struct sigaction act;
	act.sa_sigaction = [] (int sig, siginfo_t *si, void *uc) {
	  ProfilerStop();
	  if ((oldact.sa_flags & SA_SIGINFO))
	    oldact.sa_sigaction(sig, si, uc);
	  else
	    oldact.sa_handler(sig);
	};
	act.sa_flags = SA_SIGINFO;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT, &act, &oldact);
      }
    }

    __attribute__((destructor)) static void profile_stop(void) {
      if (profile_active) {
	ProfilerStop();
	profile_active = false;
      }
    }
    

    llvm::cl::opt<bool> log_times {
      "clou-times",
      llvm::cl::desc("Log execution times of Mitigate Pass"),
    };

    static void handle_timeout(int sig) {
      (void) sig;
      assert(sig == SIGALRM);
      dprintf(STDERR_FILENO, "COMPILATION TIME EXCEEDED\n");
      std::exit(EXIT_FAILURE);
    }

    llvm::cl::opt<unsigned> Timeout {
      "clou-mitigation-pass-timeout",
      llvm::cl::Hidden,
      llvm::cl::init(0),
      llvm::cl::callback([] (const unsigned& t) {
	if (t > 0) {
	  signal(SIGALRM, handle_timeout);
	  alarm(t);
	}
      }),
    };

    void print_debugloc(llvm::raw_ostream& os, const llvm::Value *V) {
      if (const auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	if (const auto& DL = I->getDebugLoc()) {
	  DL.print(os);
	  return;
	}
      }
      os << "(none)";
    }

    std::string str_debugloc(const llvm::Value *V) {
      std::string s;
      llvm::raw_string_ostream ss(s);
      print_debugloc(ss, V);
      return s;
    }

    struct Node {
      llvm::Value *V;
      Node() {}
      Node(llvm::Instruction *V): V(V) {}
      auto operator<=>(const Node&) const = default;
    };

    llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const Node& v) {
      return os << *v.V;
    }

#if 0
    std::ostream& operator<<(std::ostream& os, const Node& v) {
      llvm::raw_os_ostream os_(os);
      os_ << v;
      return os;
    }
#endif

    struct NodeHash {
      auto operator()(const Node& n) const {
	return llvm::hash_value(n.V);
      }
    };

    using Alg = MinCutGreedy<Node>;
    using Edge = Alg::Edge;
    using ST = Alg::ST;
    
    static void checkCutST(llvm::ArrayRef<std::set<Node>> st, const std::set<Edge>& cutset, llvm::Function& F) {
      assert(st.size() >= 2);
      const auto succs = [&cutset] (const Node& u) {
	std::set<Node> succs;
	for (llvm::Instruction *I : llvm::successors_inst(llvm::cast<llvm::Instruction>(u.V))) {
	  const Node v(I);
	  const Edge e = {.src = u, .dst = v};
	  if (!cutset.contains(e))
	    succs.insert(v);
	}
	return succs;
      };
      std::vector<std::set<Node>> particles;
      std::vector<std::map<Node, Node>> parents;
      std::set<Node> S = st.front();
      for (const std::set<Node>& T : st.drop_front()) {
	auto& parent = parents.emplace_back();
	particles.push_back(S);
	
	// Find all nodes reachable from S.
	std::set<Node> reach;
	std::stack<Node> todo;

	// Add all the successors of sources but not sources themselves in order to be able to capture cases where s = t.
	for (const Node& u : S)
	  todo.push(u);

	while (!todo.empty()) {
	  const Node u = todo.top();
	  todo.pop();
	  for (const Node v : succs(u)) {
	    if (reach.insert(v).second) {
	      todo.push(v);
	      parent[v] = u;
	    }
	  }
	}

	S.clear();
	std::set_intersection(T.begin(), T.end(), reach.begin(), reach.end(), std::inserter(S, S.end()));
      }
      particles.push_back(S);

      if (!S.empty()) {
	llvm::WithColor::error() << "found s-t path!\n";

	// Try to find path.
	std::vector<Node> path;
	Node t = *particles.back().begin();
	for (const auto& [parent, S] : llvm::reverse(llvm::zip(parents, llvm::ArrayRef(particles).drop_back()))) {
	  Node v = t;
	  while (true) {
	    path.push_back(v);
	    v = parent.at(v);
	    if (S.contains(v))
	      break;
	  }
	  t = v;
	}
	path.push_back(t);

	/*
	 * 1, 2, 3, ... -- indicates index into st group.
	 * s -- source of cut
	 * d -- destination of cut
	 * * -- along path.
	 */

	auto& os = llvm::errs();
	for (llvm::BasicBlock& B : F) {
	  B.printAsOperand(os);
	  os << ":\n";
	  for (llvm::Instruction& I : B) {
	    for (char c = '0'; const std::set<Node>& group : st) {
	      os << (group.contains(Node(&I)) ? c : ' ');
	      ++c;
	    }
	    {
	      const auto it = llvm::find(path, Node(&I));
	      os << (it != path.end() ? '*' : ' ');
	    }
	    {
	      const auto it = llvm::find_if(cutset, [&I] (const auto& p) {
		return p.src == Node(&I);
	      });
	      os << (it != cutset.end() ? 's' : ' ');
	    }
	    {
	      const auto it = llvm::find_if(cutset, [&I] (const auto& p) {
		return p.dst == Node(&I);
	      });
	      os << (it != cutset.end() ? 'd' : ' ');
	    }
	    os << "  " << I << "\n";
	  }
	  os << "\n";
	}
	
	llvm::errs() << "Path:\n";
	for (const Node& v : llvm::reverse(path)) {
	  llvm::errs() << "    " << *v.V << "\n";
	}
	
	std::abort();
      }
    }
    
    static void checkCut(llvm::ArrayRef<ST> sts, const std::set<Edge>& cutset, llvm::Function& F) {
      if (const char *s = std::getenv("NOCHECKCUT"))
	if (std::string(s) == "1")
	  return;
      for (const ST& st : sts)
	checkCutST(st.waypoints, cutset, F);
    }

    struct MitigatePass final : public llvm::FunctionPass {
      static inline char ID = 0;
    
      MitigatePass() : llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
	AU.addRequired<ConstantAddressAnalysis>();
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      static unsigned compute_edge_weight([[maybe_unused]] llvm::Instruction *src, llvm::Instruction *dst, [[maybe_unused]] const llvm::DominatorTree& DT, const llvm::LoopInfo& LI) {
	if (WeightGraph) {
	  float score = 1.;
#if 1
	  const unsigned LoopDepth = std::min(instruction_loop_nest_depth(src, LI), instruction_loop_nest_depth(dst, LI));
# if 0
	  const unsigned DomDepth = std::max(instruction_dominator_depth(src, DT), instruction_dominator_depth(dst, DT));
# else
	  const unsigned DomDepth = 1;
# endif
#else
	  const unsigned LoopDepth = instruction_loop_nest_depth(dst, LI);
	  const unsigned DomDepth = instruction_dominator_depth(dst, DT);
#endif
	  score *= pow(LoopDepth + 1, LoopWeight);
	  score *= 1. / pow(DomDepth + 1, DominatorWeight);
	  return score * 1000;
	} else {
	  return 1;
	}
      }

      template <class OutputIt>
      OutputIt getPublicLoads(llvm::Function& F, ConstantAddressAnalysis& CAA, OutputIt out) {
	LeakAnalysis& LA = getAnalysis<LeakAnalysis>();
	SpeculativeTaint& ST = getAnalysis<SpeculativeTaint>();
	for (auto& I : llvm::instructions(F)) {
	  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	    if (LA.mayLeak(LI) && (!ST.secret(LI) || CAA.isConstantAddress(LI->getPointerOperand()))) {
	      *out++ = LI;
	    }
	  }
	}
	return out;
      }

      static void getNonConstantAddressSecretStores(llvm::Function& F, NonspeculativeTaint& NST, SpeculativeTaint& ST,
						    ConstantAddressAnalysis& CAA, 
						    std::set<llvm::StoreInst *>& nca_nt_sec_stores, std::set<llvm::StoreInst *>& nca_t_sec_stores,
						    std::set<llvm::StoreInst *>& nca_pub_stores) {
	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	    if (!CAA.isConstantAddress(SI->getPointerOperand())) {
	      if (llvm::Instruction *V = llvm::dyn_cast<llvm::Instruction>(SI->getValueOperand())) {
		bool nt_sec = false;
		bool t_sec = false;
		for (auto *op_V : get_incoming_loads(V)) {
		  if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op_V))
		    if (auto *op_LI = llvm::dyn_cast<llvm::LoadInst>(op_I))
		      if (op_LI->getPointerOperand() == SI->getPointerOperand())
			continue;
		  if (NST.secret(op_V)) {
		    nt_sec = true;
		    break;
		  }
		  if (ST.secret(op_V))
		    t_sec = true;
		}

		if (nt_sec && !NST.secret(V)) {
		  llvm::WithColor::warning() << "mismatch in nt-secret labels\n";
		  continue;
		}
		if (t_sec && !ST.secret(V)) {
		  llvm::WithColor::warning() << "mismatch in t-secret labels\n";
		  continue;
		}

		if (nt_sec)
		  nca_nt_sec_stores.insert(SI);
		else if (t_sec)
		  nca_t_sec_stores.insert(SI);
		else
		  nca_pub_stores.insert(SI);
	      }
	    }
	  }
	}
      }

      static std::set<llvm::Instruction *> getSourcesForNCAAccess(llvm::Instruction *I,
								  [[maybe_unused]] const std::set<llvm::StoreInst *>& nca_pub_stores) {
	assert(I != &I->getFunction()->front().front() && "I cannot be the entrypoint instruction of the function");
	// compute reach set
	// NOTE: We specifically don't count the first instruction `I` as reaching itself in step 0.
	// For `I` to be reached, there must be a cycle I -> I in the CFG.
	std::set<llvm::Instruction *> reach;
	{
	  std::stack<llvm::Instruction *> todo;
	  todo.push(I);
	  while (!todo.empty()) {
	    auto *I = todo.top();
	    todo.pop();
	    for (auto *pred : llvm::predecessors(I))
	      if (reach.insert(pred).second)
		todo.push(pred);
	  }
	  assert(reach.contains(&I->getFunction()->front().front()));
	}

	// compute candidate sources
	std::set<llvm::Instruction *> sources;

	// Type 1: values used to compute address.
	for (llvm::Value *op_V : get_incoming_loads(util::getPointerOperand(I))) {
	  if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op_V)) {
	    sources.insert(op_I);
	  } else if (llvm::isa<llvm::Argument>(op_V)) {
	    auto *EntryInst = &I->getFunction()->front().front();
	    assert(I != EntryInst);
	    assert(reach.contains(EntryInst));
	    sources.insert(EntryInst);
	  } else {
	    unhandled_value(*op_V);
	  }
	}

	// Type 2: Control-flow.
	// TODO: Can use more optimal analysis of control-equivalent uses of base pointer.
	for (auto *T : reach) {
	  if (T->isTerminator()) {
	    const bool unreachable_succ = llvm::any_of(llvm::successors_inst(T), [&reach] (auto *succ) {
	      return !reach.contains(succ);
	    });
	    if (unreachable_succ)
	      sources.insert(T);
	  }
	}

	// Type 3: Calls.
	for (auto *I : reach)
	  if (auto *C = llvm::dyn_cast<llvm::CallBase>(I))
	    if (util::mayLowerToFunctionCall(*C))
	      sources.insert(C);
	
	std::erase_if(sources, [&reach] (llvm::Instruction *I) {
	  return !reach.contains(I);
	});

	// Also do backward frontier
	std::set<llvm::Instruction *> actual_sources;
#if 0
	// We will hopefully do this in the ST optimizations in MinCut.
	util::getFrontierBwd(I, sources, actual_sources);
#else
	actual_sources = std::move(sources);
#endif
	
	return actual_sources;
      }

      template <class OutputIt>
      static OutputIt getCtrls(llvm::Function& F, OutputIt out) {
	for (auto& I : llvm::instructions(F)) {
	  if (llvm::CallBase *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	    if (util::mayLowerToFunctionCall(*CB))
	      *out++ = CB;
	  } else if (llvm::isa<llvm::ReturnInst>(&I)) {
	    *out++ = &I;
	  }
	}
	return out;
      }

      static void getTransmitters(llvm::Function& F, SpeculativeTaint& ST, std::map<llvm::Instruction *, ISet>& out) {
	for (auto& I : llvm::instructions(F)) {
	  for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	    if (ST.secret(op.V)) {
	      auto *op_I = llvm::cast<llvm::Instruction>(op.V);
	      out[&I].insert(op_I);
	    }
	  }
	}
      }

      template <class OutputIt>
      static void checkArgs(llvm::Function& F, OutputIt out) {
	llvm::DataLayout DL(F.getParent());
	unsigned counter = 0;
	for (llvm::Argument& A : F.args()) {
	  if (counter >= 6) {
	    *out++ = std::make_pair(&A, false);
	  } else {
	    llvm::Type *T = A.getType();
	    const auto bits = DL.getTypeSizeInBits(T);
	    constexpr auto regbits = 64;
	    counter += ((bits + regbits - 1) / regbits);
	    *out++ = std::make_pair(&A, counter <= 6);
	  }
	}
      }

      void staticStats(llvm::json::Object& j, llvm::Function& F) {
	// List callees
	auto& callees = j["callees"] = llvm::json::Array();
	auto& indirect_calls = j["indirect_calls"] = false;
	std::set<const llvm::Function *> seen;
	for (const llvm::CallBase& CB : util::instructions<llvm::CallBase>(F)) {
	  if (const llvm::Function *F = CB.getCalledFunction()) {
	    if (seen.insert(F).second) {
	      std::string s;
	      llvm::raw_string_ostream os(s);
	      os << F->getName() << " (";
	      if (F->isDeclaration())
		os << "decl";
	      else
		os << "def";
	      os << ", " << util::linkageTypeToString(F->getLinkage()) << ")";
	      callees.getAsArray()->push_back(llvm::json::Value(s));
	    }
	  } else {
	    indirect_calls = true;
	  }
	}

	// Print which arguments are constant-address?
	auto& CAA = getAnalysis<ConstantAddressAnalysis>();
	auto& ca_args = j["ca_args"] = llvm::json::Array();
	for (const llvm::Argument *A : CAA.getConstAddrArgs(&F)) {
	  std::string s;
	  llvm::raw_string_ostream os(s);
	  os << *A;
	  ca_args.getAsArray()->push_back(llvm::json::Value(s));
	}

	// Get all transmitters
	CountStat stat_naive_loads(j, "naive_loads");
	CountStat stat_naive_xmits(j, "naive_xmits");
	for (llvm::Instruction& I : llvm::instructions(F)) {
	  bool is_xmit = false;
	  for (const auto& op : get_transmitter_sensitive_operands(&I)) {
	    if (op.kind == TransmitterOperand::TRUE) {
	      for ([[maybe_unused]] const auto& V : get_incoming_loads(op.V)) {
		is_xmit = true;
		++stat_naive_loads;
	      }
	    }
	  }
	  if (is_xmit)
	    ++stat_naive_xmits;
	}

	j["does_not_recurse"] = F.doesNotRecurse();
      }

      void saveLog(llvm::json::Object&& j, llvm::Function& F) {
	if (!ClouLog)
	  return;
	std::ofstream os_cxx = openFile(F, ".json");
	llvm::raw_os_ostream os_llvm(os_cxx);
	os_llvm << llvm::json::Value(std::move(j));
      }

      bool runOnFunction(llvm::Function &F) override {
		
	if (whitelisted(F))
	  return false;

	clock_t t_start = clock();
	
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& LA = getAnalysis<LeakAnalysis>();
	auto& CAA = getAnalysis<ConstantAddressAnalysis>();

	
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);

	// Set of speculatively public loads	
	std::set<llvm::LoadInst *> spec_pub_loads;
	getPublicLoads(F, CAA, std::inserter(spec_pub_loads, spec_pub_loads.end()));
	
	// Set of secret, speculatively out-of-bounds stores (speculative or nonspeculative)
	std::set<llvm::StoreInst *> nca_nt_sec_stores, nca_t_sec_stores, nca_pub_stores;
	getNonConstantAddressSecretStores(F, NST, ST, CAA, nca_nt_sec_stores, nca_t_sec_stores,
					  nca_pub_stores);

	// Set of control-transfer instructions that require all previous OOB stores to have resolved
	ISet ctrls;
	getCtrls(F, std::inserter(ctrls, ctrls.end()));

	// Set of transmitters
	std::map<llvm::Instruction *, ISet> transmitters;
	getTransmitters(F, ST, transmitters);

	using Alg = MinCutGreedy<Node>;
	Alg A;
	Alg::Graph& G = A.G;

#if 0
	const auto cull_sts = [] (std::vector<Alg::ST>& stvec) {
	  return;
	  unsigned culled = 0;

	  std::set<Alg::ST> sts(stvec.begin(), stvec.end());
	  
	  std::map<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>, std::set<Alg::ST>> groups;
	  for (const auto& st : sts)
	    groups[std::make_pair(st.s.V->getParent(), st.t.V->getParent())].insert(st);

	  // Approach: replace s-t pairs. 
	  for (const auto& [bbs, set] : groups) {
	    if (bbs.first != bbs.second) {
	      for (const Alg::ST& st : set) {
		sts.erase(st);
		++culled;
	      }
	      const Alg::ST newst = {
		.s = Node(&bbs.first->back()),
		.t = Node(&bbs.second->front())
	      };
	      sts.insert(newst);
	      --culled;
	    }
	  }

	  // Now find (s1, t1), (s2, t2) such that s2 postdominates s1 and t2 dominates t1.
	  {
	    std::set<Node> sources, sinks;
	    for (const Alg::ST& st : sts) {
	      sources.insert(st.s);
	      sinks.insert(st.t); 
	    }
	      
	  }
	  
	  llvm::errs() << "culled " << culled << "\n";

	  stvec.clear();
	  llvm::copy(sts, std::back_inserter(stvec));
	};
#endif
	
	/* Stats */
	llvm::json::Object log;

	CountStat stat_ncas_xmit(log, "sts_ncas_xmit");
	CountStat stat_ncas_ctrl(log, "sts_ncas_ctrl");
	CountStat stat_ncal_xmit(log, "sts_ncal_xmit");
	CountStat stat_instructions(log, "instructions", std::distance(llvm::inst_begin(F), llvm::inst_end(F)));
	CountStat stat_nonspec_secrets(log, "maybe_nonspeculative_secrets",
				       llvm::count_if(llvm::instructions(F), [&] (auto& I) { return NST.secret(&I); }));
	CountStat stat_nonspec_publics(log, "definitely_nonspeculative_public",
				       llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !NST.secret(&I); }));
	CountStat stat_spec_secrets(log, "maybe_speculative_secrets",
				    llvm::count_if(llvm::instructions(F), [&] (auto& I) { return ST.secret(&I); }));
	CountStat stat_spec_publics(log, "definitely_speculative_publics",
				    llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !ST.secret(&I); }));
	CountStat stat_leaks(log, "maybe_leaked_instructions",
			     llvm::count_if(llvm::instructions(F), [&] (auto& I) { return LA.mayLeak(&I); }));
	CountStat stat_nonleaks(log, "definitely_not_leaked_instructions",
				llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !LA.mayLeak(&I); }));
	CountStat stat_leaked_spec_secrets(log, "maybe_leaked_speculative_secret_loads",
					   llvm::count_if(util::instructions<llvm::LoadInst>(F), [&] (auto& I) {
					     return LA.mayLeak(&I) && ST.secret(&I);
					   }));


	{
	  // Transmitter stats
	  CountStat stat_xmit_pub(log, "xmit_safe");
	  CountStat stat_xmit_pseudo(log, "xmit_pseudo");
	  CountStat stat_xmit_true(log, "xmit_true");
	  for (auto& I : llvm::instructions(F)) {
	    enum Kind {
	      NONE = 0,
	      SAFE,
	      PSEUDO,
	      TRUE,
	    };
	    Kind kind = NONE;
	    for (const auto& op : get_transmitter_sensitive_operands(&I)) {
	      if (ST.secret(op.V)) {
		switch (op.kind) {
		case TransmitterOperand::TRUE:
		  kind = std::max(kind, TRUE);
		  break;
		case TransmitterOperand::PSEUDO:
		  kind = std::max(kind, PSEUDO);
		  break;
		default: std::abort();
		}
	      } else {
		kind = std::max(kind, SAFE);
	      }
	    }

	    switch (kind) {
	    case NONE: break;
	    case SAFE: ++stat_xmit_pub; break;
	    case PSEUDO: ++stat_xmit_pseudo; break;
	    case TRUE: ++stat_xmit_true; break;
	    default: std::abort();
	    }
	    
	  }
	}
	
	
	{
	  CountStat stat_cas(log, "cas");
	  CountStat stat_ncas_pub(log, "ncas_pub");
	  CountStat stat_ncas_sec(log, "ncas_sec");
	  
	  for (auto& SI : util::instructions<llvm::StoreInst>(F)) {
	    if (CAA.isConstantAddress(SI.getPointerOperand())) {
	      ++stat_cas;
	    } else {
	      auto *V = SI.getValueOperand();
	      if (NST.secret(V) || ST.secret(V)) {
		++stat_ncas_sec;
	      } else {
		++stat_ncas_pub;
	      }
	    }
	  }

	  CountStat stat_cal(log, "cal");
	  CountStat stat_ncal_leak(log, "ncal_leak");
	  CountStat stat_ncal_safe(log, "ncal_safe");
	  CountStat stat_cal_specsec(log, "cal_specsec");
	  CountStat stat_cal_leak(log, "cal_leak");

	  for (auto& LI : util::instructions<llvm::LoadInst>(F)) {
	    if (CAA.isConstantAddress(LI.getPointerOperand())) {
	      ++stat_cal;
	      if (LA.mayLeak(&LI)) {
		++stat_cal_leak;
	      }
	      if (ST.secret(&LI)) {
		++stat_cal_specsec;
	      }
	    } else {
	      assert(ST.secret(&LI));
	      if (LA.mayLeak(&LI)) {
		++stat_ncal_leak;
	      } else {
		++stat_ncal_safe;
	      }
	    }
	  }
	}

	const auto make_node_range = [] (const auto& iset) {
	  return llvm::map_range(iset, [] (llvm::Instruction *I) -> Node {
	    return {I};
	  });
	};
	const auto make_node_set = [&make_node_range] (const auto& irange) {
	  std::set<Node> nodes;
	  llvm::transform(irange, std::inserter(nodes, nodes.end()), [] (llvm::Instruction *I) -> Node {
	    return {I};
	  });
	  return nodes;
	};

	std::map<llvm::Instruction *, std::set<llvm::Instruction *>> sources_map;
	const auto get_sources = [&] (llvm::Instruction *ncal) -> const std::set<llvm::Instruction *>& {
	  auto it = sources_map.find(ncal);
	  if (it == sources_map.end()) {
	    auto sources = getSourcesForNCAAccess(ncal, nca_pub_stores);
	    it = sources_map.emplace(ncal, std::move(sources)).first;
	  }
	  return it->second; 
	};	
	
	if (enabled.ncas_xmit) {

	  // Create ST-pairs for {oob_sec_stores X spec_pub_loads}
	  CountStat stat_spec_pub_loads(log, "spec_pub_loads", spec_pub_loads.size());

	  std::set<llvm::Instruction *> ncas;
	  llvm::copy(nca_nt_sec_stores, std::inserter(ncas, ncas.end()));
	  llvm::copy(nca_t_sec_stores, std::inserter(ncas, ncas.end()));
	  if (NCASAll)
	    for (auto& SI : util::instructions<llvm::StoreInst>(F))
	      if (!CAA.isConstantAddress(SI.getPointerOperand()) && !llvm::isa<llvm::Constant>(SI.getValueOperand()))
		ncas.insert(&SI);
	  for (llvm::MemCpyInlineInst& MCII : util::instructions<llvm::MemCpyInlineInst>(F))
	    ncas.insert(&MCII);	  
	  
	  for (auto *SI : ncas) {
#if 0
	    // Compute source(s)
	    std::set<llvm::Instruction *> sources;
	    {
	      // initial sources: deps + nca pub stores
	      std::set<llvm::Value *> all_sources = get_incoming_loads(SI->getPointerOperand());
	      llvm::copy(nca_pub_stores, std::inserter(sources, sources.end()));
	      util::getFrontierBwd(SI, all_sources, sources);
	    }
#elif 0
	    const auto sources = getSourcesForNCAAccess(SI, nca_pub_stores);
#endif
	    
	    // Find instructions that the store may reach.
	    std::stack<llvm::Instruction *> todo;
	    todo.push(SI);
	    std::set<llvm::Instruction *> seen;
	    while (!todo.empty()) {
	      llvm::Instruction *I = todo.top();
	      todo.pop();
	      if (!seen.insert(I).second)
		continue;
	      for (llvm::Instruction *succ : llvm::successors_inst(I))
		todo.push(succ);
	    }

	    // Get relevant transmitter instructions.
	    std::set<Node> xmits;
	    for (llvm::Instruction *T : seen) {
	      const auto sensitive_operands = get_transmitter_sensitive_operands(T);
	      const bool vulnerable = llvm::any_of(sensitive_operands, [&] (const TransmitterOperand& TO) -> bool {
		const auto loads = get_incoming_loads(TO.V);
		return llvm::any_of(loads, [&] (auto *V) {
		  if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(V))
		    return seen.contains(LI);
		  else
		    return false;
		});
	      });
	      if (vulnerable)
		xmits.insert(T);
	    }

	    if (ExpandSTs) { 
	      const auto& sources = get_sources(SI);
	      A.add_st(make_node_set(sources), std::set<Node>{SI}, xmits);
	    } else {
	      A.add_st(std::set<Node>{SI}, xmits);
	    }
	    ++stat_ncas_xmit;
	  }

	}


	if (enabled.ncas_ctrl) {

	  // OPT NOTE: For some reason, this seems to make overall performance worse.
	  if (ExpandSTs && false) {

	    for (llvm::StoreInst *SI : llvm::concat<llvm::StoreInst * const>(nca_nt_sec_stores, nca_t_sec_stores)) {
	      // Find sources.
	      const auto& sources = get_sources(SI);
	      A.add_st(make_node_set(sources), std::set<Node>{SI}, make_node_set(ctrls));
	    }

	  } else {
	    std::set<llvm::Instruction *> ncas;
	    llvm::copy(nca_nt_sec_stores, std::inserter(ncas, ncas.end()));
	    llvm::copy(nca_t_sec_stores, std::inserter(ncas, ncas.end()));
	    if (NCASAll)
	      for (auto& SI : util::instructions<llvm::StoreInst>(F))
		if (!CAA.isConstantAddress(SI.getPointerOperand()) && !llvm::isa<llvm::Constant>(SI.getValueOperand()))
		  ncas.insert(&SI);
	    for (llvm::MemCpyInlineInst& MCII : util::instructions<llvm::MemCpyInlineInst>(F))
	      ncas.insert(&MCII);
	    // Create ST-pairs for {oob_sec_stores X ctrls}
	    A.add_st(make_node_set(ncas),
		     make_node_set(ctrls));
	  }
	}

	if (enabled.ncal_xmit) {

	  // Create ST-pairs for {source X transmitter}
	  for (const auto& [xmit, xmit_ops] : transmitters) {
	    std::set<llvm::Instruction *> ncals;
	    for (llvm::Instruction *xmit_op : xmit_ops)
	      llvm::copy(ST.taints.at(xmit_op), std::inserter(ncals, ncals.end()));
	    for (llvm::Instruction *ncal : ncals) {
	      if (!ExpandSTs || ncal == &ncal->getFunction()->front().front()) {
		A.add_st(std::set<Node>{ncal}, std::set<Node>{xmit});
		++stat_ncal_xmit;
	      } else {
		const auto& sources = get_sources(ncal);
		A.add_st(make_node_set(sources), std::set<Node>{ncal}, std::set<Node>{xmit});
		++stat_ncal_xmit;
	      }
	    }
	  }
	  
	}

	if (enabled.ncal_glob) {

	  for (llvm::StoreInst& SI : util::instructions<llvm::StoreInst>(F)) {
	    llvm::Value *SV = SI.getValueOperand();
	    if (CAA.isConstantAddress(SI.getPointerOperand()) && util::isGlobalAddressStore(&SI) &&
		!NST.secret(SV) && ST.secret(SV)) {
	      for (llvm::Instruction *LI : ST.taints.at(llvm::cast<llvm::Instruction>(SV))) {
		A.add_st(std::set<Node>{LI}, std::set<Node>{&SI});
	      }
	    }
	  }
	  
	}

	if (enabled.entry_xmit) {
	  std::set<Node> xmits;

	  // Get the pre-call transmitters.
	  {
	    std::stack<llvm::Instruction *> todo;
	    todo.push(&F.front().front());
	    std::set<llvm::Instruction *> seen;
	    while (!todo.empty()) {
	      auto *I = todo.top();
	      todo.pop();
	      if (!seen.insert(I).second)
		continue;
	      if (auto *C = llvm::dyn_cast<llvm::CallBase>(I))
		if (util::mayLowerToFunctionCall(*C))
		  continue;
	      for (const auto& [kind, xmit_op] : get_transmitter_sensitive_operands(I))
		for (llvm::Value *SourceV : get_incoming_loads(xmit_op))
		  if (auto *SourceLI = llvm::dyn_cast<llvm::LoadInst>(SourceV))
		    if (CAA.isConstantAddress(SourceLI->getPointerOperand()))
		      xmits.insert(I);
	      for (auto *Succ : llvm::successors_inst(I))
		todo.push(Succ);
	    }
	  }

	  // Get the post-call transmitters.
	  {
	    std::stack<llvm::Instruction *> todo;
	    for (auto& C : util::instructions<llvm::CallBase>(F))
	      todo.push(&C);
	    std::set<llvm::Instruction *> seen;
	    while (!todo.empty()) {
	      auto *I = todo.top();
	      todo.pop();
	      if (!seen.insert(I).second)
		continue;
	      for (const auto& [kind, xmit_op] : get_transmitter_sensitive_operands(I))
		if (!llvm::isa<llvm::Constant>(xmit_op))
		  xmits.insert(I);
	      for (auto *Succ : llvm::successors_inst(I))
		todo.push(Succ);
	    }
	  }

	  A.add_st(std::set<Node>{&F.front().front()}, xmits);
	}

	if (enabled.load_xmit) {

	  // {load} x {dependent transmitters}
	  for (llvm::Instruction& xmit : llvm::instructions(F)) {
	    std::set<llvm::Instruction *> sources;
	    for (const auto& [kind, xmit_op] : get_transmitter_sensitive_operands(&xmit))
	      for (llvm::Value *SourceV : get_incoming_loads(xmit_op))
		if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(SourceV))
		  sources.insert(LI);
	    A.add_st(make_node_set(sources), std::set<Node>{&xmit});
	  }

	  // {call} x {transmitters}
	  {
	    std::set<llvm::Instruction *> xmits;
	    std::set<llvm::CallBase *> calls;
	    for (llvm::Instruction& xmit : llvm::instructions(F)) 
	      for (const auto& [kind, xmit_op] : get_transmitter_sensitive_operands(&xmit))
		if (!llvm::isa<llvm::Constant>(xmit_op)) // Could also check if it's defined before the call.
		  xmits.insert(&xmit);
	    for (llvm::CallBase& call : util::instructions<llvm::CallBase>(F))
	      if (util::mayLowerToFunctionCall(call))
		calls.insert(&call);
	    A.add_st(make_node_set(calls), make_node_set(xmits));
	  }

	  // ret
	  for (llvm::ReturnInst& ret : util::instructions<llvm::ReturnInst>(F)) {
	    std::set<llvm::Instruction *> sources;
	    llvm::copy(llvm::predecessors(&ret), std::inserter(sources, sources.end()));
	    if (sources.empty())
	      sources.insert(&F.front().front());
	    A.add_st(make_node_set(sources), std::set<Node>{&ret});
	  }

	  // non-direct-only call
	  for (llvm::CallBase& call : util::instructions<llvm::CallBase>(F)) {
	    if (!util::mayLowerToFunctionCall(call))
	      continue;
	    if (const auto *CalledF = util::getCalledFunction(&call))
	      if (util::functionIsDirectCallOnly(*CalledF))
		continue;

	    std::set<llvm::Instruction *> sources;
	    llvm::copy(llvm::predecessors(&call), std::inserter(sources, sources.end()));
	    if (sources.empty())
	      sources.insert(&F.front().front());
	    A.add_st(make_node_set(sources), std::set<Node>{&call});
	  }

	  // {entry + call} x {stack access}
	  // NOTE: I think this is redundant since we're already inserting LFENCEs on exit.
	  if (false) {
	    std::set<llvm::Instruction *> entries;
	    for (llvm::Instruction& I : llvm::instructions(F)) {
	      if (llvm::predecessors(&I).empty()) {
		entries.insert(&I);
		continue;
	      }
	      if (auto *CI = llvm::dyn_cast<llvm::CallBase>(&I)) {
		if (!util::mayLowerToFunctionCall(*CI))
		  continue;
		entries.insert(&I);
	      }
	    }

	    std::set<llvm::Instruction *> stacks;
	    for (llvm::Instruction& I : llvm::instructions(F))
	      if (util::isStackAccess(&I))
		stacks.insert(&I);

	    A.add_st(make_node_set(entries), make_node_set(stacks));
	  }
	    
	}

	// LLSCT-SSBD
	if (enabled.call_xmit) {
	  std::set<llvm::Instruction *> xmits;
	  std::set<llvm::CallBase *> calls;
	  for (llvm::Instruction& xmit : llvm::instructions(F)) 
	    for (const auto& [kind, xmit_op] : get_transmitter_sensitive_operands(&xmit))
	      if (!llvm::isa<llvm::Constant>(xmit_op)) // Could also check if it's defined before the call.
		xmits.insert(&xmit);
	  for (llvm::CallBase& call : util::instructions<llvm::CallBase>(F))
	    if (util::mayLowerToFunctionCall(call))
	      calls.insert(&call);
	  A.add_st(make_node_set(calls), make_node_set(xmits));	  
	}

	// Add CFG to graph
	for (auto& B : F) {
	  for (auto& src : B) {
	    auto& dsts = G[&src];
	    for (auto *dst : llvm::successors_inst(&src))
	      dsts[dst] = compute_edge_weight(&src, dst, DT, LI);
	  }
	}

#if 0
	// Add back edges to CFG
	for (llvm::ReturnInst& RI : util::instructions<llvm::ReturnInst>(F)) {
	  llvm::Instruction *Entry = &F.front().front();
	  G[&RI][Entry] = compute_edge_weight(&RI, Entry, DT, LI);
	}
#else
	// Add all back edges to the S-CFG.
	{
	  const auto exits = llvm::make_filter_range(llvm::instructions(F), [] (const llvm::Instruction& I) {
	    if (llvm::isa<llvm::ReturnInst>(&I))
	      return true;
	    if (auto *C = llvm::dyn_cast<llvm::CallBase>(&I))
	      return util::mayLowerToFunctionCall(*C);
	    return false;
	  });
	  const auto entries = llvm::make_filter_range(llvm::instructions(F), [] (const llvm::Instruction& I) {
	    if (&I == &I.getFunction()->front().front())
	      return true;
	    if (auto *C = llvm::dyn_cast_or_null<llvm::CallBase>(I.getPrevNode()))
	      if (util::mayLowerToFunctionCall(*C))
		return true;
	    return false;
	  });

	  for (auto& exit : exits)
	    for (auto& entry : entries)
	      if (&entry != &exit)
		G[&exit].emplace(&entry, 1); // NOTE: We intentionally don't overwrite the previous value, since it may have been already added and contain a better edge weight. 
	}
#endif

	// Run algorithm to obtain min-cut
	const clock_t solve_start = clock();
#if 0
	cull_sts(sts);
#endif
	auto G_ = G;
	const auto sts_bak = A.get_sts().vec();
	std::cerr << "Min-Cut on " << F.getName().str() << std::endl;
	A.run();
	const clock_t solve_stop = clock();
	const float solve_duration = (static_cast<float>(solve_stop) - static_cast<float>(solve_start)) / CLOCKS_PER_SEC;
	auto& cut_edges = A.cut_edges;

	// double-check cut: make sure that no source can reach its sink
	{
	  std::set<Edge> cutset;
	  llvm::copy(cut_edges, std::inserter(cutset, cutset.end()));
	  checkCut(sts_bak, cutset, F);
	}

	// Output DOT graph, color cut edges
	if (ClouLog) {
	  
	  // emit dot
#if 0
	  {
	    std::stringstream path;
	    path << ClouLogDir << "/" << F.getName().str() << ".dot";
	    std::ofstream f(path.str());
	    f << "digraph {\n";

	    // collect stores + sinks
	    std::map<Node, std::size_t> nodes;
	    for (llvm::BasicBlock& B : F) {
	      for (llvm::Instruction& I : B) {
		nodes.emplace(&I, nodes.size());
	      }
	    }

	    std::map<Node, std::string> special;
	    for (const auto& st : A.get_sts()) {
	      static const char *colors[] = {"green", "yellow", "red"};
	      for (int i = 0; const auto& group : st.waypoints) {
		const char *color = colors[i];
		for (const Node& v : group) {
		  special[v] = color;
		}
		++i;
	      }
	    }
	    
	    for (const auto& [node, i] : nodes) {
	      f << "node" << i << " [label=\"" << node << "\"";
	      if (special.contains(node)) {
		f << ", style=filled, fontcolor=white, fillcolor=" << special[node] << "";
	      }
	      f << "];\n";
	    }

#if 0
	    // Add ST-pairs as dotted gray edges
	    for (const auto& st : A.get_sts()) {
	      for (const auto& [srcs, dsts] : llvm::zip(llvm::ArrayRef(st.waypoints).drop_back(),
							llvm::ArrayRef(st.waypoints).drop_front())) {
		for (const Node& src : srcs) {
		  for (const Node& dst : dsts) {
		    f << "node" << nodes.at(st.s) << " -> node" << nodes.at(st.t) << " [style=\"dashed\", color=\"blue\", penwidth=3]\n";
		  }
		}
	      }
	    }
#endif
	  
            
	    for (const auto& [u, usucc] : G_) {
	      for (const auto& [v, weight] : usucc) {
		if (weight > 0) {
		  f << "node" << nodes.at(u) << " -> " << "node" << nodes.at(v) << " [label=\"" << weight << "\", penwidth=3";
		  if (std::find(cut_edges.begin(),
				cut_edges.end(),
				Alg::Edge({.src = u, .dst = v})) != cut_edges.end()) {
		    f << ", color=\"red\"";
		  }
		  f << "];\n";
		}
	      }
	    }
            
	    f << "}\n";
	  }
#endif

	  // emit stats
	  {
	    log["function_name"] = F.getName();
	    log["solution_time"] = util::make_string_std(std::setprecision(3), solve_duration) + "s";
	    log["num_sts_unopt"] = sts_bak.size();
	    log["num_sts_opt"] = A.get_sts().size();
#if 0
	    VSet sources, sinks;
	    for (const auto& st : A.sts) {
	      sources.insert(st.s.V);
	      sinks.insert(st.t.V);
	    }
	    log["distinct_sources"] = sources.size();
	    log["distinct_sinks"] = sinks.size();
#endif

#if 0
	    // TODO: Re-add support for this.
	    auto& j_sts = log["st_pairs"] = llvm::json::Array();
	    for (const auto& st : A.sts) {
	      std::string source, sink;
	      llvm::raw_string_ostream source_os(source), sink_os(sink);
	      print_debugloc(source_os, st.s.V);
	      print_debugloc(sink_os, st.t.V);
	      source_os << "   (" << *st.s.V << ")";
	      sink_os   << "   (" << *st.t.V << ")";
	      j_sts.getAsArray()->push_back(llvm::json::Object({
		    {.K = "source", .V = source},
		    {.K = "sink", .V = sink},
		  }));
	    }
#endif

	    if (F.getName() == "OPENSSL_strcasecmp") {
	      llvm::json::Array j_sts;
	      for (const auto& st : sts_bak) {
		llvm::json::Array j_waypoints;
		for (const std::set<Node>& waypoint : st.waypoints) {
		  llvm::json::Array j_waypoint;
		  for (const Node& v : waypoint) {
		    std::string s;
		    llvm::raw_string_ostream os(s);
		    v.V->print(os);
		    j_waypoint.push_back(s);
		  }
		  j_waypoints.push_back(llvm::json::Value(std::move(j_waypoint)));
		}
		j_sts.push_back(llvm::json::Value(std::move(j_waypoints)));
	      }
	      log["st_pairs"] = llvm::json::Value(std::move(j_sts));
	    }

	    // TODO: emit this to separate file?
	    auto& j_cuts = log["cut_edges"] = llvm::json::Array();
	    for (const auto& cut : cut_edges) {
	      std::string src, dst;
	      llvm::raw_string_ostream src_os(src), dst_os(dst);
	      print_debugloc(src_os, cut.src.V);
	      print_debugloc(dst_os, cut.dst.V);
	      src_os << "   (" << *cut.src.V << ")";
	      dst_os << "   (" << *cut.dst.V << ")";
	      j_cuts.getAsArray()->push_back(llvm::json::Object({
		    {.K = "src", .V = src},
		    {.K = "dst", .V = dst},
		  }));
	    }

	    
	    log["lfences"] = cut_edges.size();

	    auto& j_ncas_nt_sec = log["ncas_nt_sec"] = llvm::json::Array();
	    for (auto *SI : nca_nt_sec_stores)
	      j_ncas_nt_sec.getAsArray()->push_back(util::make_string_llvm(*SI));

	    auto& j_ncas_t_sec = log["ncas_t_sec"] = llvm::json::Array();
	    for (auto * SI : nca_t_sec_stores)
	      j_ncas_t_sec.getAsArray()->push_back(util::make_string_llvm(*SI));
	  }
	}

	// Mitigations
	auto& lfence_srclocs = log["lfence_srclocs"] = llvm::json::Array();
	for (const auto& [src, dst] : cut_edges) {
	  if (llvm::Instruction *mitigation_point = getMitigationPoint(llvm::cast<llvm::Instruction>(src.V), llvm::cast<llvm::Instruction>(dst.V))) {
	    std::string s;
	    llvm::raw_string_ostream os(s);
	    const auto print_debug_loc = [&os] (const llvm::Value *V, bool forward) {
	      const llvm::Instruction *I = llvm::cast<llvm::Instruction>(V);
	      llvm::DebugLoc DL;
	      while (I != nullptr && !DL) {
		DL = I->getDebugLoc();
		if (forward)
		  I = I->getNextNode();
		else
		  I = I->getPrevNode();
	      }
	      DL.print(os);
	    };
	    print_debug_loc(src.V, false);
	    os << "--->";
	    print_debug_loc(dst.V, true);
	    CreateMitigation(mitigation_point, s.c_str());
	    
	    // Print out mitigation info
	    if (ClouLog) {
	      lfence_srclocs.getAsArray()->push_back(llvm::json::Object({
		    {.K = "src", .V = str_debugloc(src.V)},
		    {.K = "dst", .V = str_debugloc(dst.V)},
		  }));
	    }
	  }
	}

	if (ClouLog) {
	  std::ofstream f = openFile(F, ".ll");
	  llvm::raw_os_ostream os(f);
	  F.print(os);
	}

#if 0
	// Mark all public values as nospill
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (!(ST.secret(&I) || I.getType()->isVoidTy())) {
	      I.setMetadata("clou.nospill", llvm::MDNode::get(F.getContext(), {}));
	    }
	  }
	}
#endif

	
	
	const clock_t t_stop = clock();
	if (log_times) {
	  trace("time %.3f %s", static_cast<float>(t_stop - t_start) / CLOCKS_PER_SEC, F.getName().str().c_str());
	}

	staticStats(log, F);
	saveLog(std::move(log), F);

        return true;
      }

#if 1
      static bool shouldCutEdge([[maybe_unused]] llvm::Instruction *src, llvm::Instruction *dst) {
	// return llvm::predecessors(dst).size() > 1 || llvm::successors_inst(src).size() > 1;
	return llvm::predecessors(dst).size() > 1;
      }

      static llvm::Instruction *getMitigationPoint(llvm::Instruction *src, llvm::Instruction *dst) {
	if (shouldCutEdge(src, dst)) {
	  assert(src->isTerminator());
	  for (const llvm::Instruction *I = dst->getPrevNode(); I != nullptr; I = I->getPrevNode())
	    assert(llvm::isa<MitigationInst>(I));
	  if (dst != &dst->getParent()->front())
	    return nullptr;
	  llvm::BasicBlock *B = llvm::SplitEdge(src->getParent(), dst->getParent());
	  return &B->front();
	} else {
	  return dst;
	}
      }
#else
      static llvm::Instruction *getMitigationPoint(llvm::Value *src, llvm::Value *dst) {
	if (auto *dst_B = llvm::dyn_cast<llvm::BasicBlock>(dst)) {
	  // Check if we should cut the edge.
	  if (llvm::pred_size(dst_B) > 1) {
	    // Split theedge.
	    auto *src_I = llvm::cast<llvm::Instruction>(src);
	    llvm::BasicBlock *B = llvm::SplitEdge(src_I->getParent(), dst_B);
	    return &B->front();
	  } else {
	    // We can just insert at the front of the existing block.
	    return &dst_B->front();
	  }
	} else if (auto *I = llvm::dyn_cast<llvm::Instruction>(dst)) {
	  return I;
	} else {
	  llvm_unreachable("this is impossible");
	}
      }
#endif

      template <class STs>
      static void collapseOptimization(const STs& sts, VSetSet& src_out, VSetSet& sink_out) {
	VSet src_in, sink_in;
	for (const auto& st : sts) {
	  src_in.insert(st.s.V);
	  sink_in.insert(st.t.V);
	}
  
	VMap s_ts, t_ss;
	for (const auto& st : sts) {
	  s_ts[st.s.V].insert(st.t.V);
	  t_ss[st.t.V].insert(st.s.V);
	}

	// Flip maps
	std::map<std::set<llvm::Value *>, std::set<llvm::Value *>> ts_to_ss, ss_to_ts;
	for (const auto& [s, ts] : s_ts) {
	  ts_to_ss[ts].insert(s);
	}
	for (const auto& [t, ss] : t_ss) {
	  ss_to_ts[ss].insert(t);
	}

	// Copy out sets
	for (const auto& [_, ss] : ts_to_ss) {
	  src_out.insert(ss);
	}
	for (const auto& [_, ts] : ss_to_ts) {
	  sink_out.insert(ts);
	}

	// Validate results
	{
	  VSet src_out_flat, sink_out_flat;
	  for (const auto& gsrc : src_out) {
	    src_out_flat.insert(gsrc.begin(), gsrc.end());
	  }
	  for (const auto& gsink : sink_out) {
	    sink_out_flat.insert(gsink.begin(), gsink.end());
	  }
#if 0
	  if (src_out_flat != src_in || sink_out_flat != sink_in) {
	    auto& os = llvm::errs();
	    os << "sts:\n";
	    for (const auto& st : sts) {
	      os << "  (" << st.s.V << ", " << st.t.V << ")\n";
	    }
	    os << "src_in: " << src_in << "\n";
	    os << "sink_in: " << sink_in << "\n";
	    os << "s_ts: " << s_ts << "\n";
	    os << "t_ss: " << t_ss << "\n";
	    os << "src_out: " << src_out << "\n";
	    os << "sink_out: " << sink_out << "\n";
	    
	  }
#endif
	  assert(src_out_flat == src_in);
	  assert(sink_out_flat == sink_in);
	}
      }
    };

    llvm::RegisterPass<MitigatePass> X{"clou-mitigate",
					   "Clou Mitigation Pass"};
    util::RegisterClangPass<MitigatePass> Y;
  }
}

