#ifndef GENSYNTHC_H
#define GENSYNTHC_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace llvm{
    ModulePass* createGenSynthCPass(llvm::raw_ostream &OS,bool targetCPU);
}



#endif // GENSYNTHC_H
