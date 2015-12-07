clang -c ../../test_spmv.cpp 
clang -c with_cf_dup_cpu.cpp -I../../../../include/dpp/
clang -lstdc++ test_spmv.o with_cf_dup_cpu.o -pthread -o test_spmv_dpp.out

