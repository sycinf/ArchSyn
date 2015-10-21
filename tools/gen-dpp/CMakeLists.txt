set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} bitreader asmparser bitwriter irreader instrumentation scalaropts objcarcopts ipo vectorize decoupleinsscc gensynthc)

add_llvm_tool(gen-dpp
  dppgen.cpp
  )
set_target_properties(gen-dpp PROPERTIES ENABLE_EXPORTS 1)
