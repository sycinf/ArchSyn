#ifndef ARCHSYN_UTIL_H
#define ARCHSYN_UTIL_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DerivedTypes.h"
#define TRANSFORMEDATTR "dpp-transformed"
#define GENERATEDATTR "dpp-generated"
//#define CHANNELATTR "dpp-channel"
#define CHANNELWR "dpp-channel-wr"
#define CHANNELRD "dpp-channel-rd"
#define NORMALARGATTR "dpp-normalArg"
#define HLSDIRVARNAME "common_anc_dir"
#define BURST_INTSIZE 32
using namespace llvm;

bool cmpChannelAttr(AttributeSet as, int argSeqNum, std::string channelStr);
bool isArgChannel(Argument* curFuncArg);

#endif
