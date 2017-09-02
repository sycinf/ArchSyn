$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtdconst.cpp -emit-llvm -o fdtdconst.bc
$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtdconst.cpp -emit-llvm -S -o fdtdconst.ll
$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtd.cpp -emit-llvm -o fdtd.bc
$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtd.cpp -emit-llvm -S -o fdtd.ll


#$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd.bc -S -o fdtd_2.ll
#$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd.bc -o fdtd_2.bc
#$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_2.bc -S -o fdtd_3.ll
#$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_2.bc -o fdtd_3.bc
