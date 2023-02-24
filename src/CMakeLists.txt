include(RegisterLLVMPass)

add_compile_options(-fno-rtti -Wall -Wextra -fcf-protection=full -UNDEBUG)

include_directories(include)

add_link_options(-pthread)
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

message("LLVM_INCLUDE_DIRS=${LLVM_INCLUDE_DIRS}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${Libprofiler_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Libprofiler_LIBRARY_DIRS})
link_libraries(${Libprofiler_LIBRARIES})

add_subdirectory(analysis)

add_library(FordFulkerson STATIC
  FordFulkerson.cc
)
register_llvm_pass(FordFulkerson)


add_library(Metadata SHARED
  Metadata.cc
  include/clou/Metadata.h
)

add_library(util SHARED
  util.cc
  include/clou/util.h
)
target_link_libraries(util PRIVATE Metadata)

add_library(MinCut SHARED
  MinCutBase.cc
  include/clou/MinCutBase.h
  include/clou/MinCutSMT.h
)
target_link_libraries(MinCut INTERFACE ${Z3_LIBRARIES})
target_link_libraries(MinCut PUBLIC util FordFulkerson)
target_link_directories(MinCut INTERFACE ${Z3_LIBRARY_DIRS})
target_include_directories(MinCut INTERFACE ${Z3_INCLUDE_DIRS})

add_library(Mitigation SHARED
  Mitigation.cc
)
target_link_libraries(Mitigation PRIVATE Metadata ConstantAddressAnalysis)

add_library(CommandLine SHARED
  CommandLine.cc
)
register_llvm_pass(CommandLine)

add_library(Log SHARED
  Log.cc
  include/clou/Log.h
)


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

# add_library(ZeroFillCalls SHARED
#   ZeroFillCalls.cc
#   $<TARGET_OBJECTS:InstructionPass>
#   util
# )
# register_llvm_pass(ZeroFillCalls)

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

# add_library(BaselinePass SHARED
#   BaselinePass.cc
# )
# register_llvm_pass(BaselinePass)
# target_link_libraries(BaselinePass PRIVATE Mitigation util)

add_library(NoSpillPublic SHARED
  NoSpillPublic.cc
)
register_llvm_pass(NoSpillPublic)
target_link_libraries(NoSpillPublic PRIVATE util LeakAnalysis SpeculativeTaintAnalysis Mitigation)

add_library(MitigatePass SHARED
  MitigatePass.cc
)
register_llvm_pass(MitigatePass)
target_link_libraries(MitigatePass PRIVATE util Mitigation Transmitter NonspeculativeTaintAnalysis SpeculativeTaintAnalysis CommandLine LeakAnalysis ${Z3_LIBRARIES} MinCut cfg)

add_library(FunctionLocalStacks SHARED
  FunctionLocalStacks.cc
)
register_llvm_pass(FunctionLocalStacks)
target_link_libraries(FunctionLocalStacks PRIVATE util)
target_compile_options(FunctionLocalStacks PRIVATE -Wno-mismatched-new-delete)

add_library(Attributes SHARED
  Attributes.cc
)
register_llvm_pass(Attributes PRIVATE util)

add_library(NoCalleeSavedRegistersPass SHARED
  NoCalleeSavedRegistersPass.cc
)
register_llvm_pass(NoCalleeSavedRegistersPass)
target_link_libraries(NoCalleeSavedRegistersPass PRIVATE util)

add_subdirectory(check)

add_library(InlinePass SHARED
  InlinePass.cc
)
register_llvm_pass(InlinePass)
target_link_libraries(InlinePass PRIVATE util NonspeculativeTaintAnalysis SpeculativeTaintAnalysis LeakAnalysis)

add_library(DuplicatePass SHARED
  DuplicatePass.cc
)
register_llvm_pass(DuplicatePass)
target_link_libraries(DuplicatePass PRIVATE util)

# add_library(StatsPass SHARED
#   StatsPass.cc
# )
# register_llvm_pass(StatsPass)
# target_link_libraries(StatsPass PRIVATE util)


add_library(cfg SHARED
  CFG.cc
)


pkg_check_modules(Libprofiler REQUIRED libprofiler)

add_library(Profiler SHARED
  Profiler.cc
)
target_include_directories(Profiler PRIVATE ${Libprofiler_INCLUDE_DIRS})
target_link_directories(Profiler PRIVATE ${Libprofiler_LIBRARY_DIRS})
target_link_libraries(Profiler PRIVATE ${Libprofiler_LIBRARIES})

if(0)
  add_library(NCAStorePass SHARED
    NCAStorePass.cc
  )
  register_llvm_pass(NCAStorePass)
  target_link_libraries(NCAStorePass PRIVATE FordFulkerson util Mitigation Transmitter)
endif()

add_library(FramePromotion SHARED
  FramePromotion.cc
)
register_llvm_pass(FramePromotion)
target_link_libraries(FramePromotion PRIVATE util)

add_library(MemIntrinsicPass SHARED
  MemIntrinsicPass.cc
)
register_llvm_pass(MemIntrinsicPass)
target_link_libraries(MemIntrinsicPass PRIVATE util ConstantAddressAnalysis)
