#ifndef GENERATENEWINSTRUCTIONS_H
#define GENERATENEWINSTRUCTIONS_H
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
            assert( (~remoteSrc||~remoteDst) && "instruction coming in from another partition and going out to another partition");
        }
        // map all the way from a original bb to the new bb in the new function
        // can potentially map twice, 1. due to br target replacement 2. new function
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
                if(owner->originalConst2NewConst.find(oldConst)==owner->originalConst2NewConst.end())
                    // make a new constant? just return the old one
                    owner->originalConst2NewConst[oldConst] = oldConst;

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
        BasicBlock* searchNewIncomingBlock(BasicBlock* originalPred)
        {
            // we wanna find the corresponding incoming BB for the originalPred
            assert(owner->oldBB2newBBMapping.find(originalPred)!=owner->oldBB2newBBMapping.end()
                    &&"incoming block was not kept");
            return owner->oldBB2newBBMapping[originalPred];
        }

        Instruction* generatePhiNode(IRBuilder<>& builder)
        {
            errs()<<"generate phi\n";
            PHINode* pInst = &(cast<PHINode>(*originalInsn));
            PHINode* nPhi = builder.CreatePHI(pInst->getType(),pInst->getNumIncomingValues());
            for(unsigned i =  0; i<pInst->getNumIncomingValues(); i++)
            {
                Value* curInValue = pInst->getIncomingValue(i);
                BasicBlock* curInBlock = pInst->getIncomingBlock(i);
                Value* newInValue = findValueInNewFunction(curInValue);
                BasicBlock* newInBB = searchNewIncomingBlock(curInBlock);
                nPhi->addIncoming(newInValue,newInBB);
            }
            errs()<<"finish generate phi\n";
            return nPhi;
        }
        Instruction* generateSelectOperation(IRBuilder<>& builder)
        {
            SelectInst* originalSelect = &(cast<SelectInst>(*originalInsn));
            Value* oldCond = originalSelect->getCondition();
            Value* newCond = findValueInNewFunction(oldCond);

            Value* oldTrue = originalSelect->getTrueValue();
            Value* newTrue = findValueInNewFunction(oldTrue);

            Value* oldFalse = originalSelect->getFalseValue();
            Value* newFalse = findValueInNewFunction(oldFalse);

            Value* newSelectVal = builder.CreateSelect(newCond,newTrue,newFalse);
            SelectInst* newSelect = &(cast<SelectInst>(*newSelectVal));
            return newSelect;

        }
        Instruction* generateCmpOperation(IRBuilder<>& builder)
        {
            CmpInst* originalCmp = &(cast<CmpInst>(*originalInsn));
            Value* originalLHS = originalCmp->getOperand(0);
            Value* originalRHS = originalCmp->getOperand(1);

            Value* newLHS = findValueInNewFunction(originalLHS);
            Value* newRHS = findValueInNewFunction(originalRHS);
            Value* cmpInstVal = 0;

            if(originalInsn->getOpcode()==Instruction::ICmp)
                cmpInstVal = builder.CreateICmp(originalCmp->getPredicate(),newLHS,newRHS);
            else if(originalInsn->getOpcode()==Instruction::FCmp)
                cmpInstVal = builder.CreateFCmp(originalCmp->getPredicate(),newLHS,newRHS);
            CmpInst* cmpInst = 0;
            if(cmpInstVal)
                cmpInst = &(cast<CmpInst>(*cmpInstVal));
            return cmpInst;
        }

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
        ConstantInt* generatePushRemoteBrConst(int val2Push)
        {
            assert(owner->originalVal2ArgVal.find(originalInsn)!=owner->originalVal2ArgVal.end()&&"can't find port to push br tag to");
            IntegerType* dstNumType = cast<IntegerType>(owner->originalVal2ArgVal[originalInsn]->getType());
            ConstantInt* const2Push = ConstantInt::get(dstNumType,val2Push);
            return const2Push;
        }

        void populatePushRemoteBrTagBB(BasicBlock* newOwnerBB, ConstantInt* const2Push, BasicBlock* sucAfter)
        {
            IRBuilder<> builder(newOwnerBB);
            Value* ptrVal = owner->originalVal2ArgVal[originalInsn];
            builder.CreateStore(const2Push,ptrVal,true);
            builder.CreateBr(mapOriginalBBDest2CurrentBBDest(sucAfter));

        }
        // just check if branch remap is there
        BasicBlock* getIntraPartitionOriginalSuccessor(BasicBlock* suc)
        {
            if(owner->part->partitionBranchRemap.find(suc)!=owner->part->partitionBranchRemap.end())
            {
                return owner->part->partitionBranchRemap[suc];
            }
            return suc;
        }

        void generateNonReturnTerminator(IRBuilder<>& builder)
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
                return;
            }

            if(!owner->part->needBranchTag(originalInsn->getParent()) && !remoteDst)
            {
                // not remote and we dont need no branch tag, gonna be a unconditional jump
                // just double check
                BasicBlock* suc = getIntraPartitionOriginalSuccessor( curTermInst->getSuccessor(0));
                for(unsigned i = 1; i<curTermInst->getNumSuccessors(); i++)
                {
                    BasicBlock* curSuc = getIntraPartitionOriginalSuccessor( curTermInst->getSuccessor(i));
                    assert(suc==curSuc && "should be unconditional branch");
                }
                BasicBlock* newBB = mapOriginalBBDest2CurrentBBDest(suc);
                builder.CreateBr(newBB);
            }
            else
            {
                // now we do a select for the things to send, and send them before we branch
                if(isa<BranchInst>(*originalInsn))
                {
                    BranchInst* curBranch = &(cast<BranchInst>(*originalInsn));
                    assert(!curBranch->isUnconditional() && "no need to send unconditional token to other partitions");
                    // compute the number to send
                    Value* brCond = curBranch->getCondition();
                    Value* newBrCond = findValueInNewFunction(brCond);
                    assert(newBrCond!=0 && "no corresponding condition found");
                    if(remoteDst)
                    {
                        ConstantInt* const2Push0 = generatePushRemoteBrConst(0);
                        ConstantInt* const2Push1 = generatePushRemoteBrConst(1);
                        Value* selCreated = builder.CreateSelect(newBrCond,const2Push0,const2Push1);
                        Value* ptrVal = owner->originalVal2ArgVal[originalInsn];
                        builder.CreateStore(selCreated,ptrVal,true);
                    }
                    BasicBlock* oldIfTrue = curBranch->getSuccessor(0);
                    BasicBlock* oldIfFalse = curBranch->getSuccessor(1);
                    BasicBlock* dest0 = mapOriginalBBDest2CurrentBBDest(oldIfTrue);
                    BasicBlock* dest1 = mapOriginalBBDest2CurrentBBDest(oldIfFalse);
                    builder.CreateCondBr(newBrCond,dest0,dest1);
                }
                else if(isa<SwitchInst>(*originalInsn))
                {
                    SwitchInst* curSwitch = &(cast<SwitchInst>(*originalInsn));
                    Value* swCond = curSwitch->getCondition();
                    Value* newSwCond = findValueInNewFunction(swCond);
                    std::map<BasicBlock*, BasicBlock*> originalSuccessor2NewBB;

                    auto defaultCaseIter = curSwitch->case_default();
                    BasicBlock* defaultSuc = defaultCaseIter.getCaseSuccessor();


                    if(remoteDst) // got to do IR level branch to send different things -- and each
                                  // branch would then go to their own destination -- now we care about
                                  // what value we send out and what final succesor we end up in
                                  // we will iterate through the original successors, and say a few case
                                  // both go to that successor, we will just create one new bb
                    {
                        std::string swDstBase = originalInsn->getParent()->getName();
                        std::map<BasicBlock*, ConstantInt*> originalSuccessor2Const2Send;
                        for(unsigned int swDstInd = 0; swDstInd< curSwitch->getNumSuccessors(); swDstInd++ )
                        {
                            BasicBlock* curSuccessor = curSwitch->getSuccessor(swDstInd);
                            std::string swDstName = swDstBase+"_sw"+boost::lexical_cast<std::string>(swDstInd);

                            BasicBlock* dest = BasicBlock::Create(owner->addedFunction->getContext(),
                                                                  swDstName,owner->addedFunction);
                            if(originalSuccessor2NewBB.find(curSuccessor) == originalSuccessor2NewBB.end())
                            {
                                originalSuccessor2NewBB[curSuccessor] = dest;
                                originalSuccessor2Const2Send[curSuccessor] = generatePushRemoteBrConst(swDstInd);
                            }
                        }
                        // check every case, see which successor do they branch to and branch
                        std::set<BasicBlock*> populated;
                        BasicBlock* sendTagBB = originalSuccessor2NewBB[defaultSuc];
                        ConstantInt* constant2Send = originalSuccessor2Const2Send[defaultSuc];
                        BasicBlock* sucAfterSend = mapOriginalBBDest2CurrentBBDest(defaultSuc);
                        populatePushRemoteBrTagBB(sendTagBB,constant2Send,sucAfterSend);
                        populated.insert(sendTagBB);
                        // do the samething for everycase

                        for(auto caseIter = curSwitch->case_begin(); caseIter!=curSwitch->case_end(); caseIter++)
                        {
                            BasicBlock* curCaseSuc = caseIter.getCaseSuccessor();
                            BasicBlock* curSendTagBB = originalSuccessor2NewBB[curCaseSuc];
                            if(populated.count(curSendTagBB))
                                continue;
                            ConstantInt* curConstant2Send = originalSuccessor2Const2Send[curCaseSuc];
                            BasicBlock* curSucAfterSend = mapOriginalBBDest2CurrentBBDest(curCaseSuc);
                            populatePushRemoteBrTagBB(curSendTagBB,curConstant2Send,curSucAfterSend);
                            populated.insert(curSendTagBB);
                        }

                    }
                    else
                    {
                        // no remote push, we just recreate the switchInst with remapping of successor
                        for(unsigned int swDstInd = 0; swDstInd< curSwitch->getNumSuccessors(); swDstInd++ )
                        {
                            BasicBlock* curSuccessor = curSwitch->getSuccessor(swDstInd);
                            if(originalSuccessor2NewBB.find(curSuccessor) == originalSuccessor2NewBB.end())
                            {
                                originalSuccessor2NewBB[curSuccessor] = mapOriginalBBDest2CurrentBBDest(curSuccessor);
                            }
                        }
                    }
                    // create a new switchInst, the default case will be the new BB
                    BasicBlock* newDefaultSuc = originalSuccessor2NewBB[defaultSuc];
                    SwitchInst* newSwInst = builder.CreateSwitch(newSwCond,newDefaultSuc);
                    for(auto caseIter = curSwitch->case_begin(); caseIter!= curSwitch->case_end(); caseIter++)
                    {
                        BasicBlock* curCaseSuc = caseIter.getCaseSuccessor();
                        ConstantInt* oldCon = caseIter.getCaseValue();
                        ConstantInt* newCon = &cast<ConstantInt>(*findValueInNewFunction(oldCon));
                        BasicBlock* newCurSuc = originalSuccessor2NewBB[curCaseSuc];
                        newSwInst->addCase(newCon,newCurSuc);
                    }
                }
                else
                    assert(false && "non branch/switch control flow encoutered");
            }
        }
        void setNewInstructionMap(Instruction* newIns)
        {
            assert(owner->originalIns2NewIns.find(originalInsn) == owner->originalIns2NewIns.end() && "instruction mapping already exist");
            owner->originalIns2NewIns[originalInsn] = newIns;
        }

        void generateStatement(IRBuilder<>& builder)
        {
            errs()<<"ready to generate new statement for original ins "<<*originalInsn;
            if(originalInsn->isTerminator())
            {
                if(!isa<ReturnInst>(*originalInsn))
                {
                    assert((isa<BranchInst>(*originalInsn) || isa<SwitchInst>(*originalInsn))
                           && "only support branch and swith inst for control flow");
                    generateNonReturnTerminator(builder);
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
                {
                    Instruction* getRemoteData = generatePullRemoteData(builder);
                    setNewInstructionMap(getRemoteData);
                }
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
                        newlyGeneratedIns = generateCastOperation(builder);
                    else if(originalInsn->getOpcode()==Instruction::PHI)
                        newlyGeneratedIns = generatePhiNode(builder);
                    else if(originalInsn->getOpcode()==Instruction::Select)
                        newlyGeneratedIns = generateSelectOperation(builder);
                    else if(originalInsn->getOpcode()==Instruction::ICmp || originalInsn->getOpcode()==Instruction::FCmp)
                        newlyGeneratedIns = generateCmpOperation(builder);
                    assert(newlyGeneratedIns && "unhandled old instruction in generating new instruction");
                    setNewInstructionMap(newlyGeneratedIns);

                }
            }
        }
    };
}

#endif // GENERATENEWINSTRUCTIONS_H
