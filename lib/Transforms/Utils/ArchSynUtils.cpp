#include "llvm/Transforms/Utils/ArchSynUtils.h"
bool cmpChannelAttr(AttributeSet as, int argSeqNum, std::string channelStr)
{
    std::string argAttr = as.getAsString(argSeqNum+1);
    std::string channelAttrStr = "\"";
    channelAttrStr +=channelStr;
    channelAttrStr +="\"";
    return argAttr == channelAttrStr;
}

bool isArgChannel(Argument* curFuncArg)
{
    Function* func = curFuncArg->getParent();
    bool isWrChannel = cmpChannelAttr(func->getAttributes(), curFuncArg->getArgNo(), CHANNELWR);
    bool isRdChannel = cmpChannelAttr(func->getAttributes(), curFuncArg->getArgNo(), CHANNELRD);

    return isWrChannel || isRdChannel;
}
