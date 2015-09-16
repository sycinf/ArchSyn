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
#include "generateNewInstructions.h"
using namespace partGen;

namespace partGen{
    struct DppFunctionGenerator
    {
        DAGPartition* part;
        std::map<BasicBlock*,BasicBlock*> oldBB2newBBMapping;
        BasicBlock* extraEndBlock = 0;

        DppFunctionGenerator(DAGPartition* myPart)
        {
            part = myPart;
        }

        Function* addFunctionSignature(std::set<Value*>& topFuncArg,
                                       std::set<Instruction*>& srcInstruction,
                                       std::set<Instruction*>& instToSend,
                                       Instruction* retInstPtr,
                                       int seqNum,
                                       std::map<Value*,Value*>& originalVal2newArg)
        {
            LLVMContext& context = part->top->targetFunc->getContext();
            // let's go through all the things we need to make function signature
            Type* rtType;
            if(retInstPtr==0)
                rtType = Type::getVoidTy(context);
            else
                rtType = retInstPtr->getType();
            // the arrayRef for constructing the param list
            std::vector<Type*> paramsType;
            std::vector<Value*> originalVal;
            for(auto funcArgIter = topFuncArg.begin(); funcArgIter!=topFuncArg.end(); funcArgIter++)
            {
                originalVal.push_back(*funcArgIter);
                paramsType.push_back((*funcArgIter)->getType());

            }
            addArgTypeList(srcInstruction,paramsType,context,originalVal);
            addArgTypeList(instToSend,paramsType,context,originalVal);

            FunctionType* newFuncType = FunctionType::get(rtType,ArrayRef<Type*>(paramsType),false);
            Module* topModule = part->top->targetFunc->getParent();
            std::string originalFuncName = part->top->targetFunc->getName();
            std::string partFuncNameBase = originalFuncName+boost::lexical_cast<std::string>(seqNum);
            std::string partFuncName = partFuncNameBase;
            int vCountSuf = 0;
            while(topModule->getFunction(partFuncName))
            {
                partFuncName = partFuncNameBase+"_v"+boost::lexical_cast<std::string>(vCountSuf);
                vCountSuf++;
            }
            errs()<<"generate function signature: "<<partFuncName<<"\n";
            Constant* tmpFuncC = topModule->getOrInsertFunction(partFuncName, newFuncType);
            Function* actualNewFunc = cast<Function>(tmpFuncC);

            // populate the old value to new arg mapping
            auto  argValIter = actualNewFunc->arg_begin();
            for(auto oldValIter = originalVal.begin();
                oldValIter!=originalVal.end();
                oldValIter++, argValIter++)
            {
                Value* oldVal = *oldValIter;
                originalVal2newArg[oldVal] = argValIter;
                Value* curArg = argValIter;
                // name every argument
                if(isa<Argument>(*oldVal))
                    curArg->setName(oldVal->getName());
                else
                {
                    assert(isa<Instruction>(*oldVal) && "neither func arg nor instruction");
                    Instruction* oldIns = &(cast<Instruction>(*oldVal));
                    BasicBlock* oldInsBB = oldIns->getParent();
                    std::string newArgName = oldInsBB->getName();
                    //assert( oldInsIter!=originalBBEnd && "instruction cant be found in the original bb");
                    int oldInsIndex = getInstructionSeqNum(oldIns);
                    newArgName=newArgName+boost::lexical_cast<std::string>(oldInsIndex);
                    newArgName=newArgName+"_rd";
                    curArg->setName(newArgName);
                }
            }
            actualNewFunc->addFnAttr("dppcreated","true");
            return actualNewFunc;
        }


        void collectPartitionFuncArgPerBB(std::set<Value*>& topFuncArg,
                                          std::set<Instruction*>& srcInstruction,
                                          std::set<Instruction*>& instToSend,
                                          BasicBlock* curBB)
        {
            if( part->terminatorNotLocal(curBB) && part->needBranchTag(curBB)
                    && !isa<ReturnInst>(*(curBB->getTerminator())))
                srcInstruction.insert(curBB->getTerminator());
            // if this is flow only, nothing to do anymore
            if(part->isFlowOnlyBB(curBB))
                return;
            // now look at the actual content blocks
            std::set<Instruction*>* srcIns = 0;
            std::set<Instruction*>* actualIns = 0;
            if(part->sourceBBs.find(curBB)!=part->sourceBBs.end())
                srcIns = part->sourceBBs[curBB];
            if(part->insBBs.find(curBB)!=part->insBBs.end())
                actualIns = part->insBBs[curBB];
            // we look at each instruction, if the instruction is src Ins
            // but not in the actualIns we just add it to the srcInstruction set,
            // if it is an actual
            // instruction, we check to see if it is reading anything from
            // argument list of function, if it does, we add tat to the topFuncArg
            // also if it is an actual and the real owner is this partition, we
            // add the instruction to instToSend
            for(BasicBlock::iterator insPt = curBB->begin(), insEnd = curBB->end();
                insPt != insEnd; insPt++)
            {
                if(srcIns!=0 && srcIns->count(insPt))
                    if(actualIns==0 || (actualIns!=0 && !actualIns->count(insPt)))
                    {
                        Instruction* curInsPtr = insPt;
                        srcInstruction.insert(curInsPtr);
                    }
                if(actualIns!=0 &&  actualIns->count(insPt))
                {
                    bool iAmSender = true;
                    std::vector<DAGPartition*>* allOwners=part->top->getPartitionFromIns(insPt);
                    DAGPartition* realOwner = allOwners->at(0);
                    if(realOwner!=part)
                        iAmSender = false;
                    // we check the first member of the dag 2 partition map
                    // if it is not me, then I aint sender

                    if(iAmSender)
                    {
                        if(part->receiverPartitionsExist(insPt))
                            instToSend.insert(insPt);
                    }
                    // if we are using function argument, we need to add that too
                    // iterate through insPt's operand
                    for(unsigned int opInd = 0; opInd < insPt->getNumOperands(); opInd++)
                    {
                        Value* curOp = insPt->getOperand(opInd);
                        if(isa<Argument>(*curOp))
                            topFuncArg.insert(curOp);
                    }
                }
            }
        }



        void collectPartitionFuncArguments(std::set<Value*>& topFuncArg,
                                           std::set<Instruction*>& srcInstruction,
                                           std::set<Instruction*>& instToSend,
                                           ReturnInst*& returnInst)
        {
            for(auto bbIter = part->AllBBs.begin(); bbIter!= part->AllBBs.end(); bbIter++)
            {
                BasicBlock* curBB = *bbIter;
                collectPartitionFuncArgPerBB(topFuncArg,srcInstruction,instToSend,curBB);
                if(part->insBBs.find(curBB)!=part->insBBs.end())
                {
                    for(auto insIter = part->insBBs[curBB]->begin(); insIter != part->insBBs[curBB]->end(); insIter++)
                    {
                        if(isa<ReturnInst>(**insIter))
                            returnInst = &(cast<ReturnInst>(**insIter));
                    }
                }
            }

        }
        void createNewBBFromOldBB(BasicBlock* curBB, Function* addedFunction, std::set<BasicBlock*>& outsideBBs)
        {
            BasicBlock* newFuncBBEquiv = BasicBlock::Create(addedFunction->getContext(),curBB->getName(),addedFunction);
            oldBB2newBBMapping[curBB] = newFuncBBEquiv;
            // other than copying the original structure, we need to do a few more things:
            // 0. add an extra end block for who has successors outside of the partition
            //    so the boundary BBs can branch somewhere
            // 1. if we do not duplicate control flow in every partition to make tracking
            //    of execution completion easy, we need to add a while(1) to all those partition
            //    whose dominator is in a loop
            TerminatorInst* curBBTerm = curBB->getTerminator();
            for(unsigned termInd = 0; termInd < curBBTerm->getNumSuccessors(); termInd++)
            {
                BasicBlock* curSuccessor = curBBTerm->getSuccessor(termInd);
                // the successor might have been remapped
                if(part->partitionBranchRemap.find(curSuccessor)!=part->partitionBranchRemap.end())
                    curSuccessor = part->partitionBranchRemap[curSuccessor];
                if(! (part->AllBBs.count(curSuccessor)))
                    outsideBBs.insert(curSuccessor);
            }
        }

        void createNewFunctionBBs(Function* addedFunction,ReturnInst* oldReturn)
        {
            std::set<BasicBlock*> outsideBBs;
            // create dominator
            createNewBBFromOldBB(part->dominator,addedFunction,outsideBBs);
            for(auto bbIter = part->AllBBs.begin(); bbIter!= part->AllBBs.end(); bbIter++)
            {
                BasicBlock* curBB = *bbIter;
                if(curBB == part->dominator)
                    continue;
                createNewBBFromOldBB(curBB,addedFunction,outsideBBs);
            }
            if(outsideBBs.size()>0)
            {
                for(auto iter = outsideBBs.begin(); iter!=outsideBBs.end(); iter++)
                    errs()<<(*iter)->getName()<<"\n";
                extraEndBlock = BasicBlock::Create(addedFunction->getContext(),"extraEnd",addedFunction);
            }
            LoopInfo* li = part->top->getAnalysisIfAvailable<LoopInfo>();
            if(!part->top->controlFlowDuplication && li->getLoopDepth(part->dominator)!=0 && extraEndBlock)
            {
                // the extra end will always loop back to the dominator when there is a while
                // in this case the llvm function doesnt have a return statement
                IRBuilder<> builder(extraEndBlock);
                builder.CreateBr(oldBB2newBBMapping[part->dominator]);
            }
            else
            {
                // the extraEnd just return void:
                // if this partition contains the return statement
                // and either control flow is duplicated or dominator is not within loop
                // getting out of the partition means end of execution, so there will not
                // be any chance to invoke the return statement
                if(extraEndBlock)
                {
                    assert(oldReturn==0 && "Error in checking control flow structure");
                    IRBuilder<> builder(extraEndBlock);
                    builder.CreateRetVoid();
                }

            }
        }
        void populateFlowOnlyBB(Function* addedFunction, BasicBlock* curBB,
                                std::map<Value*,Value*>& originalVal2ArgVal,
                                std::map<BasicBlock*,BasicBlock*>& oldBB2newBBMapping)
        {
            BasicBlock* newFuncBBEquiv = oldBB2newBBMapping[curBB];
            IRBuilder<> builder(newFuncBBEquiv);
            TerminatorInst* oldTerm = curBB->getTerminator();
            assert(!isa<ReturnInst>(*oldTerm));
            struct InstructionGenerator ig;

            BasicBlock* originalFirstDest = oldTerm->getSuccessor(0);
            if(part->singleSucBBs.find(curBB)==part->singleSucBBs.end())
            {
                assert(originalVal2ArgVal.find(oldTerm)!=originalVal2ArgVal.end() &&
                        "cannot retrieve from functional argument remote branch tag");

                Value* receiverPtr = originalVal2ArgVal[oldTerm];
                Value* receivedDst = builder.CreateLoad(receiverPtr,true);
                IntegerType* dstNumType = cast<IntegerType>(receivedDst->getType());
                // make the first bb default
                BasicBlock* newDefaultDest = oldBB2newBBMapping[originalFirstDest];

                int numSuccessors = oldTerm->getNumSuccessors();
                SwitchInst* switchIns = builder.CreateSwitch(receivedDst,newDefaultDest,numSuccessors);
                for(unsigned int sucInd = 0; sucInd < numSuccessors; sucInd++)
                {
                    ConstantInt* curInd = ConstantInt::get(dstNumType,sucInd);
                    BasicBlock* originalCurDest = oldTerm->getSuccessor(sucInd);
                    BasicBlock* newCurDest = oldBB2newBBMapping[originalCurDest];
                    switchIns->addCase( curInd, newCurDest);
                }
            }
            else // just an unconditional/conditional branch to whoever
            {


                if(part->partitionBranchRemap.find(originalFirstDest)!=part->partitionBranchRemap.end())
                    originalFirstDest = part->partitionBranchRemap[originalFirstDest];


                BasicBlock* newRealDest = oldBB2newBBMapping[originalFirstDest];
                builder.CreateBr(newRealDest);
            }
        }



        Function* generateFunction(int seqNum)
        {
            std::set<Value*> topFuncArg;
            std::set<Instruction*> srcInstFromOtherPart;
            std::set<Instruction*> instToOtherPart;
            ReturnInst* retInstPtr = 0;
            collectPartitionFuncArguments(topFuncArg,srcInstFromOtherPart,instToOtherPart, retInstPtr);

            std::map<Value*,Value*> originalVal2NewArg;
            Function* addedFunction = addFunctionSignature(topFuncArg,srcInstFromOtherPart,instToOtherPart,retInstPtr,seqNum,originalVal2NewArg);

            createNewFunctionBBs(addedFunction,retInstPtr);
            // now we populate the newly created BBs
            /*for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++)
            {
                BasicBlock* curBB = *bbIter;
                if(isFlowOnlyBB(curBB))
                {
                    populateFlowOnlyBB(addedFunction,curBB,originalVal2NewArg,oldBB2newBBMapping);

                }
            }*/
        }


    };
}

#endif // GENERATENEWFUNCTIONS_H
