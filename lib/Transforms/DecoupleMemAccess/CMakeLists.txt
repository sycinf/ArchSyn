set(Boost_ADDITIONAL_VERSIONS "1.49" "1.49.0" "1.50" "1.50.0")

find_package( Boost  REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
endif()
message("boost: ${Boost_INCLUDE_DIRS}")

add_llvm_library( LLVMDecoupleMemAccess
  DecoupleMemAccess.cpp
  )
