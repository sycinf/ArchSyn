#ifndef DECOUPLEINSSCC_H
#define DECOUPLEINSSCC_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "decoupleInsSCC"
namespace llvm{
    FunctionPass* createDecoupleInsSccPass(bool cfDup);


}



#endif // DECOUPLEINSSCC_H
