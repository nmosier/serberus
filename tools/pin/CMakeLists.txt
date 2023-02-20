include(PinTool)

add_pin_tool(inscount inscount.cc)
add_pin_tool(bkptcount bkptcount.cc)
add_pin_tool(fninscount fninscount.cc)
add_pin_tool(proccount proccount.cc)
add_pin_tool(functions functions.cc)

configure_file(pin.sh.in pin.sh)
