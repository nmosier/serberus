function(add_pin_tool name source)
  get_filename_component(source ${source} ABSOLUTE)
  set(pintooldir "${CMAKE_CURRENT_BINARY_DIR}/${name}.pintool.d")
  make_directory("${pintooldir}")
  set(MyPinTool "${PIN_DIR}/source/tools/MyPinTool")
  foreach(filename IN ITEMS makefile makefile.rules)
    file(COPY_FILE "${MyPinTool}/${filename}" "${pintooldir}/${filename}")
  endforeach()

  get_filename_component(base ${source} NAME_WLE)
  set(so "${CMAKE_CURRENT_BINARY_DIR}/${name}.so")
  set(obj obj-intel64/${base}.so)
  add_custom_command(OUTPUT ${so}
    COMMAND cp "${source}" "${pintooldir}/${base}.cpp"
    COMMAND make -C "${pintooldir}" "PIN_ROOT=${PIN_DIR}" "${obj}"
    COMMAND cp "${pintooldir}/${obj}" "${so}"
    DEPENDS ${source}
    BYPRODUCTS ${pintooldir}/${obj}
    COMMENT "Building Pintool ${name}"
  )
  add_custom_target(${name} ALL DEPENDS ${so})
endfunction()
