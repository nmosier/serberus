include(RegisterLLVMPass)

include_directories(include)

add_compile_options(-fno-rtti -Wall -Wextra)

add_link_options(-pthread)
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

message("LLVM_INCLUDE_DIRS=${LLVM_INCLUDE_DIRS}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${Libprofiler_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Libprofiler_LIBRARY_DIRS})
link_libraries(${Libprofiler_LIBRARIES})

add_subdirectory(analysis)

add_library(Metadata SHARED
  Metadata.cc
  include/clou/Metadata.h
)

add_library(MinCut SHARED
  MinCutBase.cc
  include/clou/MinCutBase.h
  include/clou/MinCutSMT.h
)
target_link_libraries(MinCut INTERFACE ${Z3_LIBRARIES})
target_link_directories(MinCut INTERFACE ${Z3_LIBRARY_DIRS})
target_include_directories(MinCut INTERFACE ${Z3_INCLUDE_DIRS})

add_library(Mitigation SHARED
  Mitigation.cc
  include/clou/Mitigation.h
)
target_link_libraries(Mitigation PRIVATE Metadata)

add_library(CommandLine SHARED
  CommandLine.cc
)
register_llvm_pass(CommandLine)

add_library(Log SHARED
  Log.cc
  include/clou/Log.h
)

add_library(util STATIC
  util.cc
  include/clou/util.h
)
target_link_libraries(util PRIVATE Metadata)

add_library(InstructionPass OBJECT
  InstructionPass.cc
  include/clou/InstructionPass.h
)
register_llvm_pass(InstructionPass)

add_library(Transmitter OBJECT
  Transmitter.cc
  include/clou/Transmitter.h
)
target_link_libraries(Transmitter INTERFACE util)

add_library(NonspeculativeTaint SHARED
  NonspeculativeTaint.cc
  include/clou/NonspeculativeTaint.h
)
register_llvm_pass(NonspeculativeTaint)
target_link_libraries(NonspeculativeTaint PRIVATE Mitigation util Transmitter CommandLine)

add_library(ZeroFillCalls SHARED
  ZeroFillCalls.cc
  $<TARGET_OBJECTS:InstructionPass>
  util
)
register_llvm_pass(ZeroFillCalls)

add_library(TracePass SHARED
  TracePass.cc
)
register_llvm_pass(TracePass)
target_link_libraries(TracePass PRIVATE util)

add_library(Frontier SHARED
  Frontier.cc
  include/clou/Frontier.h
)
# target_link_libraries(Frontier

  
add_library(NoSpillLowering SHARED
  NoSpillLowering.cc
)
register_llvm_pass(NoSpillLowering)
target_link_libraries(NoSpillLowering PRIVATE util Metadata)

add_library(BaselinePass SHARED
  BaselinePass.cc
)
register_llvm_pass(BaselinePass)
target_link_libraries(BaselinePass PRIVATE Mitigation util)

add_library(SpeculativeTaint2 SHARED
  SpeculativeTaint2.cc
  include/clou/SpeculativeTaint2.h
)
register_llvm_pass(SpeculativeTaint2)
target_link_libraries(SpeculativeTaint2 PRIVATE util Mitigation)

add_library(AllocaInitPass SHARED
  AllocaInitPass.cc
  include/clou/AllocaInitPass.h
)
register_llvm_pass(AllocaInitPass)
target_link_libraries(AllocaInitPass PRIVATE util LeakAnalysis SpeculativeTaint2 Frontier)

add_library(NoSpillPublic SHARED
  NoSpillPublic.cc
)
register_llvm_pass(NoSpillPublic)
target_link_libraries(NoSpillPublic PRIVATE util LeakAnalysis SpeculativeTaint2 Mitigation)

add_library(MitigatePass SHARED
  Mitigate.cc
)
register_llvm_pass(MitigatePass)
target_link_libraries(MitigatePass PRIVATE util Mitigation Transmitter NonspeculativeTaint SpeculativeTaint2 CommandLine AllocaInitPass ${Z3_LIBRARIES} MinCut)

add_library(FunctionLocalStacks SHARED
  FunctionLocalStacks.cc
)
register_llvm_pass(FunctionLocalStacks)
target_link_libraries(FunctionLocalStacks PRIVATE util)

add_library(Attributes SHARED
  Attributes.cc
)
register_llvm_pass(Attributes PRIVATE util)


add_subdirectory(check)
