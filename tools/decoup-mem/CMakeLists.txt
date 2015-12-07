set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} bitreader asmparser bitwriter irreader instrumentation scalaropts objcarcopts ipo vectorize decoupleinsscc gensynthc decouplememaccess)

add_llvm_tool(decoup-mem
  decoupmem.cpp
  )
set_target_properties(decoup-mem PROPERTIES ENABLE_EXPORTS 1)
