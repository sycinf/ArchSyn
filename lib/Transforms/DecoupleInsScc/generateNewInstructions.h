#ifndef GENEREATENEWINSTRUCTIONS_H
#define GENEREATENEWINSTRUCTIONS_H
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
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
#include "generateNewInstructions.h"
#include "generateNewFunctions.h"
using namespace llvm;


namespace partGen{
    struct InstructionGenerator
    {
        Instruction* originalInsn;
        int seqNum;
        bool remoteSrc;
        bool remoteDst;
        struct DppFunctionGenerator* owner;
        InstructionGenerator(Instruction* curIns,int n, bool rs, bool rd, struct DppFunctionGenerator* fg)
        {
            originalInsn = curIns;
            seqNum = n;
            remoteSrc = rs;
            remoteDst = rd;
            owner = fg;
        }
        BasicBlock* mapOriginalBBDest2CurrentBBDest(BasicBlock* originalDest)
        {
            std::map<BasicBlock*,BasicBlock*>& destRemap= owner->part->partitionBranchRemap;
            BasicBlock* intraPartitionOriginalDest = originalDest;
            if(destRemap.find(originalDest)!=destRemap.end())
                intraPartitionOriginalDest = destRemap[originalDest];
            return owner->oldBB2newBBMapping[intraPartitionOriginalDest];
        }

        void generateGenericSwitchStatement(IRBuilder<>& builder,Value* cond,TerminatorInst* originalTerm)
        {
            IntegerType* dstNumType = cast<IntegerType>(cond->getType());
            int numSuccessor = originalTerm->getNumSuccessors();
            assert(numSuccessor>1 && "try to generate switch statement for single successor terminator ");
            BasicBlock* newDefaultDest = mapOriginalBBDest2CurrentBBDest( originalTerm->getSuccessor(0));
            SwitchInst* swInst = builder.CreateSwitch(cond,newDefaultDest);
            for(unsigned int sucInd = 0; sucInd < originalTerm->getNumSuccessors(); sucInd++)
            {
                ConstantInt* curCase = ConstantInt::get(dstNumType,sucInd);
                BasicBlock* originalDest = originalTerm->getSuccessor(sucInd);
                BasicBlock* curCaseDest = mapOriginalBBDest2CurrentBBDest(originalDest);
                swInst->addCase(curCase,curCaseDest);
            }
        }
        Value* findValueInNewFunction(Value* oldVal)
        {
            if(isa<Instruction>(*oldVal))
            {
                Instruction* oldIns = &(cast<Instruction>(*oldVal));
                if(owner->originalIns2NewIns.find(oldIns)!=owner->originalIns2NewIns.end())
                    return owner->originalIns2NewIns[oldIns];

            }
            else if(isa<Argument>(*oldVal))
            {
                if(owner->originalVal2ArgVal.find(oldVal)!=owner->originalVal2ArgVal.end())
                    return owner->originalVal2ArgVal[oldVal];

            }
            else if(isa<Constant>(*oldVal))
            {
                Constant* oldConst = &(cast<Constant>(*oldVal));
                if(owner->originalConst2NewConst.find(oldConst)!=owner->originalConst2NewConst.end())
                    return owner->originalConst2NewConst[oldConst];
            }
            return 0;

        }

        void generateReturnStatement(IRBuilder<>& builder)
        {
            ReturnInst* retInst = &(cast<ReturnInst>(*originalInsn));
            Value* oldReturnVal = retInst->getReturnValue();
            if(oldReturnVal)
            {
                // first check if this has been created by somebody internally
                // if not, we check if this is from the function argument
                // note
                Value* newlyGeneratedVal= findValueInNewFunction(oldReturnVal);

                assert(newlyGeneratedVal!=0 &&
                            "cannot find return value from locally executed instructions nor from function argument");
                builder.CreateRet(newlyGeneratedVal);
            }
            else
                builder.CreateRetVoid();

        }
        Instruction* generateBinaryOperation(IRBuilder<>& builder)
        {
            BinaryOperator* originalBinOp = &(cast<BinaryOperator>(*originalInsn));
            Value* left = findValueInNewFunction(originalBinOp->getOperand(0));
            Value* right = findValueInNewFunction(originalBinOp->getOperand(1));
            Value* newVal = builder.CreateBinOp(originalBinOp->getOpcode(),left,right);
            Instruction* newIns = &(cast<Instruction>(*newVal));
            return newIns;
        }
        Instruction* generateMemoryOperation(IRBuilder<>& builder)
        {

            switch(originalInsn->getOpcode())
            {
                case Instruction::Alloca:
                {
                    AllocaInst* aInsn = &(cast<AllocaInst>(*originalInsn));
                    return builder.CreateAlloca(aInsn->getAllocatedType(),findValueInNewFunction(aInsn->getArraySize()));

                }
                case Instruction::Load:
                {
                    LoadInst* lInsn = &(cast<LoadInst>(*originalInsn));
                    Value* newPtrVal = findValueInNewFunction( lInsn->getPointerOperand());
                    return builder.CreateLoad(newPtrVal,lInsn->isVolatile());
                }
                case Instruction::Store:
                {
                    StoreInst* sInsn = &(cast<StoreInst>(*originalInsn));
                    Value* newStoreVal = findValueInNewFunction( sInsn->getValueOperand());
                    Value* newPtrVal = findValueInNewFunction(sInsn->getPointerOperand());
                    return builder.CreateStore(newStoreVal,newPtrVal,sInsn->isVolatile());

                }
                case Instruction::GetElementPtr:
                {
                    GetElementPtrInst* gInsn = &(cast<GetElementPtrInst>(*originalInsn));
                    // create the array of idx
                    std::vector<Value*> idxList;
                    for(unsigned operandInd = 0; operandInd < gInsn->getNumOperands(); operandInd++)
                    {
                        if(operandInd == gInsn->getPointerOperandIndex())
                            continue;
                        idxList.push_back(findValueInNewFunction(gInsn->getOperand(operandInd)));
                    }
                    ArrayRef<Value*> arIdxList(idxList);
                    Value* newPtrVal = findValueInNewFunction( gInsn->getPointerOperand());
                    Value* newRtVal = builder.CreateGEP(newPtrVal,arIdxList);
                    Instruction* newRtIns = &(cast<Instruction>(*newRtVal));
                    return newRtIns;
                }
                default:
                    return 0;
            }

        }

        Instruction* generateCastOperation(IRBuilder<>& builder)
        {
            CastInst* cInst = &(cast<CastInst>(*originalInsn));
            Value* rtVal = builder.CreateCast(cInst->getOpcode(),
                                      findValueInNewFunction(originalInsn->getOperand(0)),
                                      originalInsn->getType());
            Instruction* rtIns = &(cast<Instruction>(*rtVal));
            return rtIns;
        }
        /*Instruction* generatePhiNode(IRBuilder<>& builder)
        {
            PHINode* pInst = &(cast<PHINode>(*originalInsn));
            pInst->get
        }*/

        void generatePushRemoteData(IRBuilder<>& builder)
        {
            // we must have already generated the data locally
            assert(owner->originalIns2NewIns.find(originalInsn)!=owner->originalIns2NewIns.end() &&
                    "data 2 push has not been generated locally");
            assert(owner->originalVal2ArgVal.find(originalInsn)!=owner->originalVal2ArgVal.end() &&
                    "fifo argument dont exist for pushing data");
            Instruction* newlyGeneratedInsn = owner->originalIns2NewIns[originalInsn];
            Value* ptrVal = owner->originalVal2ArgVal[originalInsn];
            builder.CreateStore(newlyGeneratedInsn,ptrVal,true);
        }
        Instruction* generatePullRemoteData(IRBuilder<>& builder)
        {
            assert(owner->originalVal2ArgVal.find(originalInsn)!=owner->originalVal2ArgVal.end() &&
                    "fifo argument dont exist for pulling data");
            Value* ptrVal = owner->originalVal2ArgVal[originalInsn];
            return builder.CreateLoad(ptrVal,true);
        }
        Value* getAppropriateValue4Use(Value* oldValue)
        {
            // this is to find the new value from the old value
            if(isa<Instruction>(*oldValue))
            {
                // there must be a instruction which generate this value
                // in the new function, actual or src
                Instruction* oldIns = &(cast<Instruction>(oldValue));
                assert(owner->originalIns2NewIns.find(oldIns)!=owner->originalIns2NewIns.end() &&
                        "unavailable value for use locally");

            }
        }

        void generateNonReturnTerminator(IRBuild<>& builder)
        {
            TerminatorInst* curTermInst = &(cast<TerminatorInst>(*originalInsn));
            if(remoteSrc)
            {
                // load from the func arg
                Value* receiverPtr = owner->originalVal2ArgVal[curTermInst];
                Value* receivedDst = builder.CreateLoad(receiverPtr,true);
                // since there is a requirement for remoteSrc
                // we definitely need a switch
                generateGenericSwitchStatement(builder,receivedDst,curTermInst);
            }

            if(remoteDst)
            {
                // we need to compute the number successor, and generate the write instruction
                if(isa<BranchInst>(*originalInsn))
                {
                    BranchInst* curBranch = &(cast<BranchInst>(*originalInsn));
                    assert(!curBranch->isUnconditional() && "no need to send unconditional token to other partitions");
                    // compute the number to send
                    Value* brCond = curBranch->getCondition();
                    std::string brDestBase = originalInsn->getParent()->getName();
                    BasicBlock* dest0 = BasicBlock::Create(owner->addedFunction->getContext(),
                                                          brDestBase+"_br0",owner->addedFunction);
                    BasicBlock* dest1 = BasicBlock::Create(owner->addedFunction->getContext(),
                                                          brDestBase+"_br1",owner->addedFunction);



                }
            }
            else
            {
                // we generate the local non return terminator
                // and possibly write the tag into the channel
                bool artificialUnconditional = false;
                std::vector<BasicBlock*>& endWithUncond = owner->part->singleSucBBs;
                if(std::find(endWithUncond.begin(),endWithUncond.end(), originalInsn->getParent())!=endWithUncond.end())
                    artificialUnconditional = true;

                rtStr = generateControlFlow(cast<TerminatorInst>(*originalInsn),remoteDst,seqNum, owner->fifoArgs, owner->functionArgs,destRemap,artificialUnconditional);
            }


        }

        void generateStatement(IRBuilder<>& builder)
        {
            //FIXME: terminator generation not done
            // all the branch condition needs to be generated
            if(originalInsn->isTerminator())
            {
                if(!isa<ReturnInst>(*originalInsn))
                {
                    assert((isa<BranchInst>(*originalInsn) || isa<SwitchInst>(*originalInsn))
                           && "only support branch and swith inst for control flow");

                    // as a terminator, we should check if this dude has
                    // any successors
                    TerminatorInst* curTermInst = &(cast<TerminatorInst>(*originalInsn));

                    if(remoteSrc)
                    {
                        // load from the func arg
                        Value* receiverPtr = owner->originalVal2ArgVal[curTermInst];
                        Value* receivedDst = builder.CreateLoad(receiverPtr,true);
                        generateGenericSwitchStatement(builder,receivedDst,curTermInst);
                    }
                    if(remoteDst)
                    {
                        // we need to compute the number successor, and generate the write instruction
                        if(isa<BranchInst>(*originalInsn))
                        {
                            BranchInst* curBranch = &(cast<BranchInst>(*originalInsn));
                            assert(!curBranch->isUnconditional() && "no need to send unconditional token to other partitions");
                            // compute the number to send
                            Value* brCond = curBranch->getCondition();
                            std::string brDestBase = originalInsn->getParent()->getName();
                            BasicBlock* dest0 = BasicBlock::Create(owner->addedFunction->getContext(),
                                                                  brDestBase+"_br0",owner->addedFunction);
                            BasicBlock* dest1 = BasicBlock::Create(owner->addedFunction->getContext(),
                                                                  brDestBase+"_br1",owner->addedFunction);



                        }
                    }
                    else
                    {
                        // we generate the local non return terminator
                        // and possibly write the tag into the channel
                        bool artificialUnconditional = false;
                        std::vector<BasicBlock*>& endWithUncond = owner->part->singleSucBBs;
                        if(std::find(endWithUncond.begin(),endWithUncond.end(), originalInsn->getParent())!=endWithUncond.end())
                            artificialUnconditional = true;

                        rtStr = generateControlFlow(cast<TerminatorInst>(*originalInsn),remoteDst,seqNum, owner->fifoArgs, owner->functionArgs,destRemap,artificialUnconditional);
                    }
                }
                else
                {
                    generateReturnStatement(builder);
                }
            }
            else // here we safely assume all the operand have been generated
                 // to achieve that, in function generator, we generate terminators first
                 // then srcInstructions,their relative sequence would ensure the execution sequence
                 // being compatible with other partitions -- no deadlock
                 // now when we start generating actualIns, we would recurse backward when operand
                 // is not available yet -- at the end, we will reorder the instructions
            {
                if(remoteSrc)
                    generateGettingRemoteData(builder);
                else
                {
                    // locally generated, let's ensure all the values have been generated -- if they are instructions
                    // in some other function
                    for(unsigned int operandInd = 0; operandInd < originalInsn->getNumOperands(); operandInd++)
                    {
                        Value* curOperand = originalInsn->getOperand(operandInd);
                        assert(0!=findValueInNewFunction(curOperand)  && "cannot find value in new function");
                    }
                    Instruction* newlyGeneratedIns=0;
                    if(originalInsn->isBinaryOp())
                        newlyGeneratedIns = generateBinaryOperation(builder);
                    else if(originalInsn->getOpcode()<Instruction::MemoryOpsEnd &&originalInsn->getOpcode() >= Instruction::MemoryOpsBegin  )
                        newlyGeneratedIns = generateMemoryOperation(builder);
                    else if(originalInsn->getOpcode()<Instruction::CastOpsEnd && originalInsn->getOpcode()>= Instruction::CastOpsBegin)
                        newlyGenerateIns = generateCastOperation(builder);
                    else if(originalInsn->getOpcode()==Instruction::PHI)
                        newlyGenerateIns = generatePhiNode(builder);
                    else if(originalInsn->getOpcode()==Instruction::Select)
                    {
                        if(remoteSrc)
                            rtStr = generateGettingRemoteData(*insn,seqNum,owner->fifoArgs);
                        else
                        {
                            rtStr = generateSelectOperations(cast<SelectInst>(*insn), remoteDst, seqNum,owner->fifoArgs,owner->functionArgs);
                        }
                    }
                    else if(insn->getOpcode()==Instruction::ICmp || insn->getOpcode()==Instruction::FCmp)
                    {

                        // got to generate the cmpare statement
                        if(remoteSrc)
                            rtStr = generateGettingRemoteData(*insn,seqNum,owner->fifoArgs);
                        else
                        {
                            rtStr = generateCmpOperations(cast<CmpInst>(*insn), remoteDst, seqNum,owner->fifoArgs,owner->functionArgs);
                        }
                    }
                    else
                    {
                        errs()<<" problem unhandled instruction in generate single statement";
                        exit(1);
                    }
                }
            }
        }
    };
}

#endif // GENEREATENEWINSTRUCTIONS_H
