source ../../../install_dir.sh
$LLVM_BIN_INSTALL_DIR/gen-sync  ../../spmv.bc -o spmv_recons.cpp -cpu-mode
