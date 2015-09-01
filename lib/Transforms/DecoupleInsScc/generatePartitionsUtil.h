#ifndef GENERATEPARTITIONSUTIL_H
#define GENERATEPARTITIONSUTIL_H

#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/IR/Constants.h"
using namespace llvm;
// FIXME: just a place holder, this function is used to
// calculate the latency through SCCs
int instructionLatencyLookup(Instruction* ins)
{
    // normal instructions like "and, or , add, shift, are assigned a value of 3, multiply assigned a value of 10"
    // load and store assigned a value of 10, and 10 means one pipeline stage,
    if(ins->mayReadFromMemory())
        return 1;
    switch(ins->getOpcode())
    {
        case Instruction::Mul:
            return 5;
        case Instruction::Br:
            return 0;
        case Instruction::Trunc:
        case Instruction::ZExt:
        case Instruction::SExt:
        case Instruction::BitCast:
            return 0;
    default:
        return 1;
    }
    return 1;
}
bool instructionExpensive(Instruction* ins)
{
    if(ins->mayReadFromMemory())
        return true;
    switch(ins->getOpcode())
    {

    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
            return true;
        default:
            return false;

    }
    return false;
}


std::string getConstantStr(Constant& original)
{
    std::string rtStr = "";
    if(isa<ConstantFP>(original))
    {
        ConstantFP& fpRef = cast<ConstantFP>(original);
        APFloat pf = fpRef.getValueAPF();
        SmallVector<char, 32> Buffer;
        pf.toString(Buffer, 10,10);

        for(SmallVector<char, 32>::iterator cur=Buffer.begin(), end=Buffer.end();cur!=end; cur++ )
            rtStr.append(1,*cur);


    }
    else if(isa<ConstantInt>(original))
    {
        ConstantInt& intRef = cast<ConstantInt>(original);
        APInt pint = intRef.getValue();
        std::string str=pint.toString(10,true);
        rtStr = rtStr +str;

    }
    else
    {
        errs()<<"unsupported constant\n";
        exit(1);
    }

    return rtStr;
}

#endif // GENERATEPARTITIONSUTIL_H
