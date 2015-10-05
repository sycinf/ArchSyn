#ifndef GENERATENEWFUNCTIONS_H
#define GENERATENEWFUNCTIONS_H
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "generatePartitionsUtil.h"
#include "generatePartitions.h"
#include <boost/lexical_cast.hpp>
#include <iterator>
using namespace partGen;

namespace partGen{
    struct DppFunctionGenerator
    {
        DAGPartition* part;
        std::map<BasicBlock*,BasicBlock*> oldBB2newBBMapping;
        BasicBlock* extraEndBlock = 0;
        Function* addedFunction;
        // some value are read from argument, we need this relation
        std::map<Value*,Value*> originalVal2ArgVal;

        // all values are generated locally -- with newly created instruction
        // some of these are load instruction reading from arg
        std::map<Instruction*,Instruction*> originalIns2NewIns;

        std::map<Constant*,Constant*> originalConst2NewConst;

        // for PhiNode, we keep generating operand till we hit another
        // phiNode

        DppFunctionGenerator(DAGPartition* myPart)
        {
            part = myPart;
        }

        Function* addFunctionSignature(std::set<Value*>& topFuncArg,
                                       std::set<Instruction*>& srcInstruction,
                                       std::set<Instruction*>& instToSend,
                                       Instruction* retInstPtr,
                                       int seqNum,
                                       std::map<Instruction*,Value*>& ins2AllocatedChannel,
                                       std::vector<Value*>* argList
                                       );

        void collectPartitionFuncArgPerBB(std::set<Value*>& topFuncArg,
                                          std::set<Instruction*>& srcInstruction,
                                          std::set<Instruction*>& instToSend,
                                          BasicBlock* curBB);


        void collectPartitionFuncArguments(std::set<Value*>& topFuncArg,
                                           std::set<Instruction*>& srcInstruction,
                                           std::set<Instruction*>& instToSend,
                                           ReturnInst*& returnInst);
        void createNewBBFromOldBB(BasicBlock* curBB,  std::set<BasicBlock*>& outsideBBs);
        void createNewFunctionBBs(bool haveReturnInst);
        void populateFlowOnlyBB(BasicBlock* curBB);
        //void populateContentBB(BasicBlock* curBB);
        void populateContentBBSrcIns(BasicBlock* curBB);
        void populateContentBBActualIns(BasicBlock* curBB);
        void generateActualInstruction(Instruction* originalIns);

        Function* generateFunction(int seqNum,
                                   std::map<Instruction*,Value*>& ins2AllocatedChannel,
                                   std::vector<Value*>* argList);

    };
}

#endif // GENERATENEWFUNCTIONS_H
