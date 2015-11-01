#ifndef DECOUPLEMEMACC_H
#define DECOUPLEMEMACC_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
// this attribute denotes a function as a decoupled mem accesss
// function where input is stream of address and "access size"
// out put is the data -- all in streaming fashion
#define DECPLEDMEMATTR "decoupled-mem-access"

namespace llvm{
    ModulePass* createDecoupleMemAccessPass(bool burst);


}



#endif // DECOUPLEINSSCC_H
