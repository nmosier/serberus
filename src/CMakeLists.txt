add_compile_options(-fno-rtti -Wall -Wextra)

link_libraries(${Z3_LIBRARIES})
link_directories(${Z3_LIBRARY_DIRS})
add_link_options(-pthread)
include_directories(${Z3_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

message("LLVM_INCLUDE_DIRS=${LLVM_INCLUDE_DIRS}")
function(register_llvm_pass TARGET)
  target_include_directories(${TARGET} SYSTEM BEFORE PUBLIC ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(${TARGET} PRIVATE ${LLVM_DEFINITIONS})
  if(APPLE)
    target_link_options(${TARGET} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${Libprofiler_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
link_directories(${Libprofiler_LIBRARY_DIRS})
link_libraries(${Libprofiler_LIBRARIES})

add_library(my_scc_pass STATIC
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

add_library(InstructionPass OBJECT
  InstructionPass.cc
  InstructionPass.h
)
register_llvm_pass(InstructionPass)

add_library(Transmitter OBJECT
  Transmitter.cc
  Transmitter.h
  )

add_library(SpeculativeTaint SHARED
  SpeculativeTaint.cc
  $<TARGET_OBJECTS:Transmitter>
  util
)
register_llvm_pass(SpeculativeTaint)
target_link_libraries(SpeculativeTaint PRIVATE CommandLine Mitigation Log)

add_library(count_spectre_v1_1 SHARED
  count_spectre_v1_1.cc
  util
  )
register_llvm_pass(count_spectre_v1_1)

add_library(mitigate SHARED
  mitigate.cc
  util
  )
register_llvm_pass(mitigate)

add_executable(min_cut_driver
  min-cut-driver.cc
  )

add_library(print_dom_loop SHARED
  print_dom_loop.cc
  util
  )
register_llvm_pass(print_dom_loop)

add_library(secret_param_analysis SHARED
  SecretParamAnalysis.cc
  util
  Transmitter
  my_scc_pass
  )
register_llvm_pass(secret_param_analysis)

add_library(NonspeculativeTaint SHARED
  NonspeculativeTaint.cc
  NonspeculativeTaint.h
  $<TARGET_OBJECTS:Transmitter>
  util
)
register_llvm_pass(NonspeculativeTaint)
target_link_libraries(NonspeculativeTaint PUBLIC my_scc_pass Mitigation)

add_library(SpectreV1_1 SHARED
  SpectreV1_1.cc
  util
  NonspeculativeTaint
  $<TARGET_OBJECTS:Transmitter>
  my_scc_pass
  )
register_llvm_pass(SpectreV1_1)

add_library(ZeroFillCalls SHARED
  ZeroFillCalls.cc
  $<TARGET_OBJECTS:InstructionPass>
  util
)
register_llvm_pass(ZeroFillCalls)

add_library(BreakPass SHARED
  BreakPass.cc
  util
)

add_library(LoopIndexPass SHARED
  LoopIndexPass.cc
  util
  metadata
)
register_llvm_pass(LoopIndexPass)

add_library(SpectreBCBSSecretPass SHARED
  SpectreBCBSSecretPass.cc
)
register_llvm_pass(SpectreBCBSSecretPass)
target_link_libraries(SpectreBCBSSecretPass PRIVATE NonspeculativeTaint util metadata CommandLine)

add_library(TracePass SHARED
  TracePass.cc
)
register_llvm_pass(TracePass)
target_link_libraries(TracePass PRIVATE util)

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

add_library(SecureOOBPass SHARED
  SecureOOBPass.cc
)
register_llvm_pass(SecureOOBPass)
target_link_libraries(SecureOOBPass PRIVATE Transmitter Mitigation util Log)
  
add_library(PrintRegions SHARED
  PrintRegions.cc
)
register_llvm_pass(PrintRegions)

add_library(MitigateDT SHARED
  MitigateDT.cc
)
register_llvm_pass(MitigateDT)
target_link_libraries(MitigateDT util Mitigation Transmitter CommandLine)

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
target_link_libraries(AllocaInitPass PRIVATE util NonspeculativeTaint SpeculativeTaint2)

add_library(MitigatePass SHARED
  Mitigate.cc
)
register_llvm_pass(MitigatePass)
target_link_libraries(MitigatePass PRIVATE util Mitigation Transmitter NonspeculativeTaint SpeculativeTaint2 CommandLine AllocaInitPass ${Z3_LIBRARIES} MinCut)
