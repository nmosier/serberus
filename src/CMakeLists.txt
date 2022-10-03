include(RegisterLLVMPass)

add_compile_options(-fno-rtti -Wall -Wextra)

link_libraries(${Z3_LIBRARIES})
link_directories(${Z3_LIBRARY_DIRS})
add_link_options(-pthread)
include_directories(SYSTEM ${Z3_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

message("LLVM_INCLUDE_DIRS=${LLVM_INCLUDE_DIRS}")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${Libprofiler_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Libprofiler_LIBRARY_DIRS})
link_libraries(${Libprofiler_LIBRARIES})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(analysis)

add_library(my_scc_pass SHARED
  my_scc_pass.cc
  my_scc_pass.h
)

add_library(metadata SHARED
  metadata.cc
  metadata.h
)

add_library(MinCut SHARED
  MinCutBase.h
  MinCutBase.cc
  MinCutSMT.h
)

add_library(Mitigation SHARED
  Mitigation.cc
  Mitigation.h
)
target_link_libraries(Mitigation PRIVATE metadata)

add_library(CommandLine SHARED
  CommandLine.cc
  CommandLine.h
)
register_llvm_pass(CommandLine)

add_library(Log SHARED
  Log.cc
  Log.h
)

add_library(util STATIC
  util.cc
  util.h
  metadata # TODO: shouldn't depend on this
)
target_include_directories(util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(InstructionPass OBJECT
  InstructionPass.cc
  InstructionPass.h
)
register_llvm_pass(InstructionPass)

add_library(Transmitter OBJECT
  Transmitter.cc
  Transmitter.h
)
target_link_libraries(Transmitter INTERFACE util)

add_library(NonspeculativeTaint SHARED
  NonspeculativeTaint.cc
  NonspeculativeTaint.h
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
  Frontier.h
)
# target_link_libraries(Frontier

  
add_library(NoSpillLowering SHARED
  NoSpillLowering.cc
)
register_llvm_pass(NoSpillLowering)
target_link_libraries(NoSpillLowering PRIVATE util metadata)

add_library(BaselinePass SHARED
  BaselinePass.cc
)
register_llvm_pass(BaselinePass)
target_link_libraries(BaselinePass PRIVATE Mitigation util)

add_library(SpeculativeTaint2 SHARED
  SpeculativeTaint2.h
  SpeculativeTaint2.cc
)
register_llvm_pass(SpeculativeTaint2)
target_link_libraries(SpeculativeTaint2 PRIVATE util Mitigation)

add_library(AllocaInitPass SHARED
  AllocaInitPass.h
  AllocaInitPass.cc
)
register_llvm_pass(AllocaInitPass)
target_link_libraries(AllocaInitPass PRIVATE util NonspeculativeTaint SpeculativeTaint2 Frontier)

add_library(NoSpillPublic SHARED
  NoSpillPublic.cc
)
register_llvm_pass(NoSpillPublic)
target_link_libraries(NoSpillPublic PRIVATE util NonspeculativeTaint SpeculativeTaint2 Mitigation)

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
