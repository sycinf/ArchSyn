$LLVM_BIN_INSTALL_DIR/clang  -m32 -c fdtd.cpp -emit-llvm -o fdtd_unopt.bc
$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd_unopt.bc -S -o fdtd_mem2reg.ll
$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd_unopt.bc -o fdtd_mem2reg.bc
$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_mem2reg.bc -S -o fdtd_ml.ll
$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_mem2reg.bc -o fdtd_ml.bc
$LLVM_BIN_INSTALL_DIR/opt -simplifycfg fdtd_ml.bc -o fdtd_mll.bc
$LLVM_BIN_INSTALL_DIR/opt -simplifycfg fdtd_ml.bc -S -o fdtd_mll.ll
#$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtd.cpp -emit-llvm -o fdtd.bc
#$LLVM_BIN_INSTALL_DIR/clang  -O2 -m32 -c fdtd.cpp -emit-llvm -S -o fdtd.ll
#$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd.bc -S -o fdtd_2.ll
#$LLVM_BIN_INSTALL_DIR/opt -mem2reg fdtd.bc -o fdtd_2.bc
#$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_2.bc -S -o fdtd_3.ll
#$LLVM_BIN_INSTALL_DIR/opt -loop-simplify fdtd_2.bc -o fdtd_3.bc
