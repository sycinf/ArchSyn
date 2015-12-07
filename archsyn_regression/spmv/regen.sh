clang -O1 -c spmv.cpp -emit-llvm -o spmv.bc
clang -O1 -c spmv.cpp -emit-llvm -S -o spmv.ll
