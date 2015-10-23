set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} bitreader asmparser bitwriter irreader instrumentation scalaropts objcarcopts ipo gensynthc)

add_llvm_tool(gen-sync
  gensync.cpp
  )
set_target_properties(gen-sync PROPERTIES ENABLE_EXPORTS 1)
