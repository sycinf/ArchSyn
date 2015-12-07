clang -c ../../test_spmv.cpp 
clang -c spmv_recons.cpp -I../../../../include/dpp/
clang test_spmv.o spmv_recons.o -o test_spmv_recons.out

