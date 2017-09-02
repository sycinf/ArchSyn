source ../../../install_dir.sh
$LLVM_BIN_INSTALL_DIR/gen-par  ../../fdtdconst.bc -DAInfo  -o dump.ll 
echo "---------------------------Finished analyzing const substituted kernels, starting on original kernel-------------------" 
$LLVM_BIN_INSTALL_DIR/gen-par  ../../fdtd.bc  -DAInfo -o dump2.ll  
