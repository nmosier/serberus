/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <fstream>
#include <map>
#include "pin.H"
using std::cerr;
using std::endl;
using std::ios;
using std::ofstream;
using std::string;

ofstream OutFile;

static std::map<ADDRINT, std::string> addr_to_name;
static std::map<std::string, unsigned> count;

static VOID handle_sentinel(ADDRINT addr) {
  const std::string& name = addr_to_name.at(addr);
  count[name]++;
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID* v) {
  if (INS_Opcode(ins) == XED_ICLASS_LFENCE) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) handle_sentinel, IARG_ADDRINT, INS_Address(ins), IARG_END);
    addr_to_name[INS_Address(ins)] = RTN_Name(INS_Rtn(ins));
  }
}

KNOB< string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "inscount.out", "specify output file name");

// This function is called when the application exits
VOID Fini(INT32 code, VOID* v) {
  for (const auto& [name, n] : count) {
    std::cout << name << " " << n << "\n";
  }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
  cerr << "This tool counts the number of breakpoints executed" << endl;
  cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*   argc, argv are the entire command line: pin -t <toolname> -- ...    */
/* ===================================================================== */

int main(int argc, char* argv[])
{
  PIN_InitSymbols();
  
  // Initialize pin
  if (PIN_Init(argc, argv)) return Usage();

  OutFile.open(KnobOutputFile.Value().c_str());

  // Register Instruction to be called to instrument instructions
  INS_AddInstrumentFunction(Instruction, 0);

  // Register Fini to be called when the application exits
  PIN_AddFiniFunction(Fini, 0);

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
