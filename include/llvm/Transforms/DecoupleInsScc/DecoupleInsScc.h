#ifndef DECOUPLEINSSCC_H
#define DECOUPLEINSSCC_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "decoupleInsSCC"
#define TRANSFORMEDATTR "dpp-transformed"
#define GENERATEDATTR "dpp-generated"
//#define CHANNELATTR "dpp-channel"
#define CHANNELWR "dpp-channel-wr"
#define CHANNELRD "dpp-channel-rd"
#define NORMALARGATTR "dpp-normalArg"
#define HLSDIRVARNAME "common_anc_dir"
namespace llvm{
    FunctionPass* createDecoupleInsSccPass(bool cfDup);


}



#endif // DECOUPLEINSSCC_H
