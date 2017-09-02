#ifndef GENPAR_H
#define GENPAR_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
namespace llvm{
    FunctionPass* createGenParPass(llvm::raw_ostream &OS, bool DAInfo);
}



#endif // GENPAR_H

