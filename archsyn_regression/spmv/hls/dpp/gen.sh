source ../../../install_dir.sh
$LLVM_BIN_INSTALL_DIR/gen-dpp  ../../spmv.bc  -ocfile with_cf_dup_hls.cpp -o with_cf_dup.ll
