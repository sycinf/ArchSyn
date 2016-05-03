set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} bitreader asmparser bitwriter irreader instrumentation scalaropts objcarcopts ipo analysis genpar)

add_llvm_tool(gen-par
  genparimpl.cpp
  )
set_target_properties(gen-par PROPERTIES ENABLE_EXPORTS 1)
