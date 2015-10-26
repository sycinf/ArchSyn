//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//
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
#include "generateNewFunctions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <boost/lexical_cast.hpp>
#include <math.h>
#include <algorithm>
using namespace llvm;

namespace boost{
    void throw_exception(std::exception const & e)
    {
        DIS_MSG<<"boost exception";
        exit(1);
    }
}

namespace partGen {
        DAGNode::~DAGNode()
        {
            /*for(auto nodeIter = dagNodeContent.begin();nodeIter!=dagNodeContent.end(); nodeIter++)
            {
                InstructionGraphNode* curIGNPtr = *nodeIter;
                delete curIGNPtr;
            }*/
            dagNodeContent.clear();
        }

        void DAGNode::init()
        {
            singleIns = false;
            sccLat = 0;
            hasMemory = false;
            expensive = false;
            covered = false;
            seqNum = -1;
        }
        void DAGNode::print()
        {
            for(std::vector<InstructionGraphNode*>::iterator curInsNIter = dagNodeContent.begin();
                curInsNIter != dagNodeContent.end();
                curInsNIter++)
            {
                Instruction* curIns = (*curInsNIter)->getInstruction();
                //DIS_MSG<<"\t"<<*curIns<<"\n";
                errs()<<*curIns<<"\n";
            }

        }
        void DAGPartition::init(struct DecoupleInsScc* tt )
        {
            containMemory = false;
            containLongLatCyc = false;
            cycleDetectCovered = false;
            top = tt;
            rInsn= NULL;
            dominator=0;
        }
        void DAGPartition::print()
        {
            for(unsigned int nodeI = 0; nodeI < partitionContent.size(); nodeI++)
            {
                DAGNode* curNode = partitionContent.at(nodeI);
                curNode->print();

            }
        }
        void DAGPartition::addDagNode(DAGNode* dagNode,DagNode2PartitionMap &nodeToPartitionMap )
        {
            partitionContent.push_back(dagNode);
            dagNode->covered=true;
            containMemory |= dagNode->hasMemory;
            containLongLatCyc |= (dagNode->sccLat >= LONGLATTHRESHOLD);
            // each dagNode maps to at least one partition
            // but maybe duplicated , this is used to search
            // for the partition from which the operand originate
            // and thus build channels
            std::vector<DAGPartition*>* listOfDp;
            if(nodeToPartitionMap.find(const_cast<DAGNode*>(dagNode)) == nodeToPartitionMap.end())
            {
                listOfDp = new std::vector<DAGPartition*>();
                nodeToPartitionMap[dagNode] = listOfDp;
            }
            else
                listOfDp = nodeToPartitionMap[dagNode];
            listOfDp->push_back(this);
        }
        void DAGPartition::generateBBList()
        {
            // at this point instructions are associated with the partition
            // each of these instructions' owner bb is of course relevant to
            // to the partition
            // each of these instructions are getting operand from somewhere:
            // either instruction or function arg, if the source is an instruction I_s
            // then I_s' owner basic block would be relevant for this partition as well
            // each map keeps track of the BBs and the contained relevant instructions
            for(unsigned int nodeInd = 0; nodeInd < partitionContent.size(); nodeInd++)
            {
                DAGNode* curNode = partitionContent.at(nodeInd);
                for(unsigned int insInd = 0; insInd< curNode->dagNodeContent.size(); insInd++)
                {
                    Instruction* curIns = curNode->dagNodeContent.at(insInd)->getInstruction();
                    if(isa<ReturnInst>(*curIns))
                        rInsn= curIns;
                    addBBInsToMap(insBBs,curIns);
                    addSrcIns(curIns,sourceBBs);
                }
            }
            // FIXME: this should be optional
            //optimizeBBByDup(sourceBBs,insBBs);


            // dominator for src and ins BBs
            // this will be the first basicblock we need to convert
            // then we shall see where each bb diverge?

            DominatorTree* DT= &(top->getAnalysisIfAvailable<DominatorTreeWrapperPass>()->getDomTree());
            dominator = findDominator(dominator,sourceBBs,DT);
            dominator = findDominator(dominator,insBBs,DT);

            //BasicBlock* postDominator =0;
            //PostDominatorTree* PDT = top->getAnalysisIfAvailable<PostDominatorTree>();
            //postDominator = findPostDominator(postDominator,sourceBBs,PDT);
            //postDominator = findPostDominator(postDominator,insBBs,PDT);

            // start from each srcBBs, search backward until dominator,
            // everything in between is to be needed, -- if they are not srcBB nor insBBs
            // their terminator output would be needed? not necessariy!
            // if a properly contained BB (BB_z) -- meaning either src or ins BB
            // postdominate some BB (BB_x) which has nothing in it...BB_x
            // can be ommited -- all the edges going to BB_x can directly go
            // to BB_z
            // how do we detect this?
            // originally, we start from the dominator, then search to every BB
            // adding everything in the path, this dominator's terminator is
            // necessarily produced by an earlier stage due to the transitive dominance
            // now -- we can start searching from dominator to a destination BB,
            // if at a certain point, a particular BB is properly postdominated
            // by the destination BB, then we can directly go from the precedessors
            // of this BB to the destination BB -- without adding any BBs in between
            //
            // how do we know where to branch to?
            // we shall have a map, the first ommited BB would be the key
            // pointing to the postdominator, this postdominator (destination)
            // can be changed, for instance, originally we set the postdominator
            // to be A, then later we search for B and found the omitted BB
            // for A --- then either A dominate B or B dominate A, we will
            // make the earlier one the value of our table, and then add
            // entries for the path between A and B/B and A
            //

            // in the case of two BBs, if one BB is never reaching another BB
            // without going through dominator, then when this BB exits, we can
            // just wait in the dominator, so the procedure should be use
            // each BB as starting point to search for all other BBs, if any BB
            // is found all path to that BB should be included? if none can be found
            // then getting out of this BB is same as getting out of our subgraph

            // naturally if we have everything within one BB, we just need that BB
            // some of these BBs wont be in the map, in which case, we just need the terminator
            AllBBs.insert(dominator);

            // add the path from the dominator to all the sourceBBs
            addPathBBsToBBMap(sourceBBs,dominator,AllBBs,dominator);
            addPathBBsToBBMap(insBBs,dominator,AllBBs,dominator);

errs()<<"\t\t\t\t\t\tafter addPathBBsToBBMap : \n";
            for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++ )
                errs()<<(*(bbIter))->getName()<<"\n";



            // now  we do the n^2 check to see if anybody goes to anybody else?
            for(auto bmi = sourceBBs.begin(), bme = sourceBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                //std::vector<BasicBlock*> curPathStorage;
                addPathBBsToBBMap(sourceBBs,curBB,AllBBs,dominator);
                addPathBBsToBBMap(insBBs,curBB,AllBBs,dominator);
                //searchToGroupAndAdd(curBB,dominator,*sourceBBs,curPathStorage,*AllBBs);
                // same thing for insBB
                //searchToGroupAndAdd(curBB,dominator,*insBBs,curPathStorage,*AllBBs);

            }
            for(BBMapIter bmi = insBBs.begin(), bme = insBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                //std::vector<BasicBlock*> curPathStorage;
                addPathBBsToBBMap(sourceBBs,curBB,AllBBs,dominator);
                addPathBBsToBBMap(insBBs,curBB,AllBBs,dominator);

                // now we shall look at the phi
                // basic blocks gets added for the reason that
                // they are the predecessor which tells phi what
                // value to output
                std::set<Instruction*>* curBBInsns = bmi->second;
                addPhiOwner2Vector(curBBInsns, AllBBs);
            }


            // a special pass to search for every insBB and srcBB themselves

            for(auto bmi = insBBs.begin(), bme = insBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                // search all its successor, this is to accommodate
                // the case where a block can branch to outside blocks
                // then come back to itself without passing through
                // dominator, if we do not take into consideration these
                // path, then the control flow doesnt have anywhere to go
                if(curBB!= dominator)
                    addPathToSelf(curBB,AllBBs,dominator);
            }
            for(auto bmi = sourceBBs.begin(), bme = sourceBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                if(curBB!= dominator)
                    addPathToSelf(curBB,AllBBs,dominator);
            }


            // make sure everybody's predecessor is either dominator or part of AllBBs
            // if they are not, add it in, and update the dominator,and add the path in
            // this is so that there is only one dominator -- of course, if somebody itself
            // is dominator, we dont need to look at its predecessor
            BB2BBVectorMapTy* predMap = top->getAnalysis<InstructionGraph>().getPredecessorMap();
            std::set<BasicBlock*> added=AllBBs;
            bool initial = true;
            while(added.size()!=0)
            {
                if(!initial)
                    AllBBs.insert(added.begin(),added.end());
                // now set dominators to be common dominator of the added ones and
                // the original dominator

                BasicBlock* originalDominator = dominator;
                dominator = findDominator(dominator,added,DT);
                if(originalDominator!=dominator)
                {
                    // we have had an update of dominator, the original dominator
                    // should have its predecessor looked at
                    added.insert(originalDominator);
                }

                initial=false;
                std::set<BasicBlock*> lastAdded = added;
                added.clear();
                for(auto curAddedIter = lastAdded.begin(); curAddedIter!=lastAdded.end(); curAddedIter++)
                {
                    BasicBlock* curAddedBB = *curAddedIter;
                    if(curAddedBB == dominator)
                        continue;
                    if(predMap->find(curAddedBB)!=predMap->end())
                    {
                        std::vector<BasicBlock*>* curBBPreds = (*predMap)[curAddedBB];
                        for(auto curPredIter = curBBPreds->begin();curPredIter!=curBBPreds->end();curPredIter++)
                        {
                            BasicBlock* curPred = *curPredIter;
                            if(!AllBBs.count(curPred) && curPred!=dominator)
                            {
                                added.insert(curPred);
                            }
                        }
                    }
                }
            }


            LoopInfo* li =top->getAnalysisIfAvailable<LoopInfo>();
            if(top->controlFlowDuplication)
            {
                // we are going to duplicate control flows, this is to make sure we dont need while loops
                // and have the end of execution trackable
                // note at this point, any loop structure between the insBBs and srcBBs are already included

                /* to make sure everything exit the loops properly, we want our sub graphs to
                 * eventually branch to blocks outside of any loop, what we can do is to see what
                 * are the basic blocks outside of all the loops --> and trace from basic blocks in our
                 * list to these blocks, again a dfs --> keep a path if it hits something among this group
                 */
                if(li->getLoopDepth(dominator)!=0)
                {
                    // actual implementation, all the strongly connected basic blocks involving
                    // anybody in the AllBBs need to be included
                    std::set<BasicBlock*> cpAllBBs = AllBBs;
                    for(std::set<BasicBlock*>::iterator existingBBIter = cpAllBBs.begin();
                        existingBBIter!=cpAllBBs.end();existingBBIter++)
                    {
                        BasicBlock* curExistingBB = *existingBBIter;
                        for (scc_iterator<Function*> SCCI = scc_begin(top->targetFunc),
                               E = scc_end(top->targetFunc); SCCI != E; ++SCCI)
                        {
                          const std::vector<BasicBlock*> &nextSCC = *SCCI;
                          if(std::find(nextSCC.begin(),nextSCC.end(),curExistingBB)!=nextSCC.end())
                          {
                              // this scc should be included
                              mergeInto(nextSCC,AllBBs);
                          }
                        }
                    }
                    dominator = findDominator(dominator,AllBBs,DT);
                }
            }
        }
        //FIXME: all the graph search like things should be ported to LLVM graph traits
        // libs
        void DAGPartition::trimBBList()
        {
            BB2BBVectorMapTy* predMap = top->getAnalysis<InstructionGraph>().getPredecessorMap();
            PostDominatorTree* PDT = top->getAnalysisIfAvailable<PostDominatorTree>();
            //DominatorTree* DT= top->getAnalysisIfAvailable<DominatorTree>();
            // we can iterate through every block
            // in the allBBs list
            // if a basicblock A  is of no instruction --- only exist for flow
            // purpose, we then look at to see if it should be discarded
            // how do we do it?
            // all these are assumed to be redundant
            // we then have a queue of keepers -- starting from all realBBs,
            // we iterate until this queue is empty,
            // we take an iterm off this queue
            // traverse backward from it(currentR), for the path, if we
            // see a non real BB(nRBB), we check if currentR postdominate nRBB
            // if the answr is no, then nRBB is a keeper, the path is done,
            // and nRBB is added to the keeper queue, the rationale here is
            // the nRBB would either branch out of the current partition
            // or goto two different BB in the current partition, if it is
            // not included, we would have problem constructing a cfg properly

            //std::vector<BasicBlock*>* allBBs = (top->allBBsInPartition)[this];
            //BBMap2Ins*  srcBBs = (top->srcBBsInPartition)[this];
            //BBMap2Ins*  insBBs = (top->insBBsInPartition)[this];
            std::set<BasicBlock*> allRealBBs;
            for(auto bmi = sourceBBs.begin(), bme = sourceBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                allRealBBs.insert(curBB);
            }
            for(auto bmi = insBBs.begin(), bme = insBBs.end(); bmi!=bme; ++bmi)
            {
                BasicBlock* curBB=bmi->first;
                allRealBBs.insert(curBB);

                // now if this is generating a result based on incoming edges
                // then the basic block incoming edge should be counted as real
                // and always be preserved
                std::set<Instruction*>* curBBInsns = insBBs[curBB];
                addPhiOwner2Vector(curBBInsns, allRealBBs);

            }
            std::set<BasicBlock*> toRemove = AllBBs;
            // queue for blocks to keep
            std::vector<BasicBlock*> toKeep;
            std::vector<BasicBlock*> allKeepers;
            toKeep.insert(toKeep.begin(),allRealBBs.begin(),allRealBBs.end());

            while(toKeep.size()>0)
            {

                BasicBlock* curSeed = toKeep.back();
                toKeep.pop_back();
                allKeepers.push_back(curSeed);
                // search backwards from curSeed, until there is a keeper
                // all everything is seen
                std::set<BasicBlock*> seenBBs;
                seenBBs.insert(curSeed);
                if(predMap->find(curSeed)!=predMap->end())
                {
                    std::vector<BasicBlock*>* curPreds = (*predMap)[curSeed];
                    for(auto predIter = curPreds->begin(); predIter!= curPreds->end(); predIter++)
                    {
                        BasicBlock* curPred = *predIter;
                        searchToFindKeeper(curSeed, curPred,predMap, toKeep, allKeepers, seenBBs,PDT, AllBBs );
                    }
                }
            }
            // we now try to build the remap
            // we start from one keeper, look for the next keeper in dfs way
            // realize every keeper is a divergent point
            // from a outgoing edge of an keeper, there can be only one keeper
            for(unsigned int keeperInd = 0; keeperInd < allKeepers.size();keeperInd++)
            {
                BasicBlock* curKeeper = allKeepers.at(keeperInd);
                TerminatorInst* termIns = curKeeper->getTerminator();
                int numOutEdge = termIns->getNumSuccessors();
                for(unsigned int brInd = 0; brInd<numOutEdge; brInd++)
                {
                    std::vector<BasicBlock*> curBranchKeeper;
                    // from this edge, we find next keeper
                    BasicBlock* brSuccessor = termIns->getSuccessor(brInd);
                    std::set<BasicBlock*> seenBBs;
                    search4NextKeeper( brSuccessor, allKeepers, curBranchKeeper, seenBBs, AllBBs );
                    assert(!(curBranchKeeper.size()>1) &&
                           "a non-included basic block diverge to multiple \
                           basic blocks, we cannot construct a proper cfg");

                    assert((!curBranchKeeper.empty()) &&
                           "an included basic block's successor does not lead \
                           to any other included basic block--not even BBs outside of the BB");
                    BasicBlock* newDst = curBranchKeeper.at(0);
                    this->partitionBranchRemap[brSuccessor] = newDst;

                }
                // now remove this keeper from toRemove
                assert(toRemove.erase(curKeeper) && "cannot remove keeper from removal list -- it is absent\
                                                       in the first place");
            }
            // now actually remove it
            for(auto bb2RemoveIter = toRemove.begin(); bb2RemoveIter!= toRemove.end(); bb2RemoveIter++)
            {
                BasicBlock* bb2Remove = *bb2RemoveIter;
                //std::vector<BasicBlock*>::iterator found = std::find(AllBBs.begin(),AllBBs.end(),bb2Remove);
                //errs()<<(*found)->getName()<<" removed " <<"\n";
                AllBBs.erase(bb2Remove);
            }
            // we shall build a map of basicblocks who now only have
            // one successor -- meaning they do not need to get remote branchtag
            // traverse every block, check their destination (with remap)
            //for(unsigned int allBBInd=0; allBBInd<AllBBs.size(); allBBInd++)
            for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++)
            {
                BasicBlock* curBB = *bbIter;
                TerminatorInst* curTermInst = curBB->getTerminator();
                if(!isa<ReturnInst>(*curTermInst))
                {
                    int numSuc = curTermInst->getNumSuccessors();
                    if(numSuc==1)
                        singleSucBBs.insert(curBB);
                    else
                    {
                        bool sameDestDu2Remap = true;
                        BasicBlock* firstDst = curTermInst->getSuccessor(0);
                        if(partitionBranchRemap.find(firstDst)!=partitionBranchRemap.end())
                            firstDst = partitionBranchRemap[firstDst];
                        for(unsigned int i = 1; i<numSuc; i++)
                        {
                            BasicBlock* curDst = curTermInst->getSuccessor(i);
                            if(partitionBranchRemap.find(curDst)!=partitionBranchRemap.end())
                                curDst = partitionBranchRemap[curDst];
                            if(curDst!=firstDst)
                            {
                                sameDestDu2Remap = false;
                                break;
                            }

                        }
                        if(sameDestDu2Remap)
                            singleSucBBs.insert(curBB);
                    }
                }
            }

        }

        void DAGPartition::checkDominator()
        {

            DominatorTree* DT= &(top->getAnalysisIfAvailable<DominatorTreeWrapperPass>()->getDomTree());
            BasicBlock* domBB = *(AllBBs.begin());
            //for(unsigned int bbInd = 1; bbInd < AllBBs.size(); bbInd++)
            for(auto bbIter = AllBBs.begin(); bbIter!=AllBBs.end(); bbIter++)
                domBB = DT->findNearestCommonDominator(domBB,*bbIter);

            assert(AllBBs.count(domBB) && "dominator check failed: dominator not in the bb list");
            assert(domBB == dominator && "dominator check failed: original dominator does not match regenerated dominator");

        }
        bool DAGPartition::allExitOutsideLoop()
        {
            LoopInfo* li = top->getAnalysisIfAvailable<LoopInfo>();
            for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++)
            {
                BasicBlock* curBB = *bbIter;
                TerminatorInst* curTerm = curBB->getTerminator();
                for(unsigned sucInd = 0; sucInd < curTerm->getNumSuccessors(); sucInd++)
                {
                    BasicBlock* curSuc = curTerm->getSuccessor(sucInd);
                    BasicBlock* actualDst=curSuc;
                    if(this->partitionBranchRemap.find(curSuc)!=this->partitionBranchRemap.end())
                        actualDst = partitionBranchRemap[curSuc];

                    if(!AllBBs.count(actualDst))
                    {
                        if(li->getLoopDepth(actualDst)!=0)
                            return false;

                    }
                }
            }
            return true;
        }
        bool DAGPartition::isFlowOnlyBB(BasicBlock* curBB)
        {
            return (sourceBBs.find(curBB)==sourceBBs.end() && insBBs.find(curBB)==insBBs.end());
        }
        bool DAGPartition::terminatorNotLocal(BasicBlock* curBB)
        {
            std::set<Instruction*>* actualIns = 0;
            if(insBBs.find(curBB)!=insBBs.end())
                actualIns = insBBs[curBB];
            return (actualIns==0 || !(actualIns->count(curBB->getTerminator())));
        }
        bool DAGPartition::needBranchTag(BasicBlock* curBB)
        {
            return !singleSucBBs.count(curBB);
        }
        bool DAGPartition::hasActualInstruction(Instruction* target)
        {
            bool haveIns = false;
            BasicBlock* curBB = target->getParent();
            if(insBBs.find(curBB)!=insBBs.end())
            {
                std::set<Instruction*>* actualIns =insBBs[curBB];
                haveIns = actualIns->count(target);
            }
            return haveIns;
        }
        bool DAGPartition::receiverPartitionsExist(Instruction* insPt)
        {
            if(insPt->isTerminator()&& !isa<ReturnInst>(*insPt))
            {
                errs()<<"checking if "<<*insPt<<" has receiver partition\n";
                // are there other partitions having the same basicblock
                // we will need to pass the branch tag over as long as it is
                // the case
                for(unsigned sid = 0; sid < top->collectedPartition.size(); sid++)
                {
                    DAGPartition* destPart = top->collectedPartition.at(sid);
                    if(this != destPart && !destPart->hasActualInstruction(insPt))
                    {
                        if(destPart->AllBBs.count(insPt->getParent()) && destPart->needBranchTag(insPt->getParent()))
                            return true;
                        errs()<<"partition "<<sid<<":";
                        errs()<<"end up no need to receive "<<destPart->AllBBs.count(insPt->getParent())<<" is count parent\n";
                    }
                }
            }
            else
            {
                for(Value::user_iterator curUser = insPt->user_begin(), endUser = insPt->user_end(); curUser != endUser; ++curUser )
                {
                    assert(isa<Instruction>(*curUser));
                    // now multiple guy can use this value

                    std::vector<DAGPartition*>* curUseOwners=top->getPartitionFromIns(cast<Instruction>(*curUser));
                    // we will iterate through each partition in the vector
                    for(unsigned int s = 0; s < curUseOwners->size(); s++)
                    {
                        DAGPartition* curUsePart = curUseOwners->at(s);
                        if(curUsePart != this  && !curUsePart->hasActualInstruction(insPt))
                            return true;
                    }
                }
            }
            return false;
        }
        void DAGPartition::setupBBStructure()
        {
            // we collect all the basic blocks
            // and remove what ever redundant BasicBlocks
errs()<<"\t\t\t\t\t\tinitial BB List : \n";
            for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++ )
                errs()<<(*(bbIter))->getName()<<"\n";

            generateBBList();


errs()<<"\t\t\t\t\t\t after generate BB List : \n";
            for(auto bbIter = AllBBs.begin(); bbIter!= AllBBs.end(); bbIter++ )
                errs()<<(*(bbIter))->getName()<<"\n";
            trimBBList();
            /**** some check to run to ensure the cfg is properly formed***/
            checkDominator();
//=========================
/*
            errs()<<"partition "<<seqNum<<" going to genFunction\n";
            for(auto bbIter = AllBBs.begin(); bbIter!=AllBBs.end(); bbIter++)
                errs()<<(*bbIter)->getName()<<"\n";
            errs()<<"actual ins bb:\n";
            for(auto actualInsIter = insBBs.begin(); actualInsIter!=insBBs.end(); actualInsIter++)
                errs()<<actualInsIter->first->getName()<<"\n";*/
//===========================
            if (top->controlFlowDuplication)
                assert(allExitOutsideLoop() && "some basic blocks fan out to non-included basic blocks \
                                               inside a loop, shouldn't happen when control flow is \
                                                is duplicated\n");

        }

        Function* DAGPartition::generateDecoupledFunction(int seqNum,
                                                          std::map<Instruction*,Value*>& ins2AllocatedChannel,
                                                          std::vector<Value*>* argList
                                                          )
        {

            /**** end of check ************/
            struct DppFunctionGenerator dpg(this);
            return dpg.generateFunction(seqNum,ins2AllocatedChannel,argList);


        }






        /*
        // this is to be invoked right after the srcBB and insBB are established
        void optimizeBBByDup(BBMap2Ins* srcBBs,BBMap2Ins* insBBs)
        {
            errs()<<"Starting optimization\n";
             //= top->srcBBsInPartition[this];
             //= top->insBBsInPartition[this];
            // now is there any way we can reduce the communication/or allow memory
            // optimization by duplicating simple counters
            // how do we do this?
            // we go through every srcBB, check, this srcBB probably belongs to a dependency cycle
            // -- if this srcBB feeds to an address, we want the address to be generated locally?
            // -- find the cycle,if it does not involve memory access, let's move it over
            // and check how many local src ins belong to this cycle, all them
            // get to become insBB
            // for now let's just dump out the involved instructions
            for(BBMapIter bmi = srcBBs->begin(), bme = srcBBs->end(); bmi!=bme; ++bmi)
            {
                std::vector<Instruction*>* curBBSrcInsns = bmi->second;
                BasicBlock* curSrcBB = bmi->first;
                std::vector<Instruction*>* curBBActualInsns = 0;
                if(insBBs->find(curSrcBB)!=insBBs->end())
                    curBBActualInsns = (*insBBs)[curSrcBB];

                errs()<<"cur source ins: \n";
                // now for each of these instructions, we do a dfs to see if they fan out to local
                // instructions accessing memory
                // if yes, we search backwards to find the scc it depends on
                // how do we check if it is a counter structure?
                // there is a circle formed by an add instruction and a phiNode

                for(unsigned int insInd = 0; insInd < curBBSrcInsns->size(); insInd++)
                {
                    Instruction* curIns = curBBSrcInsns->at(insInd);
                    // if this instruction is also in actual ins, then we dont care
                    if(curBBActualInsns!=0 && std::find(curBBActualInsns->begin(),curBBActualInsns->end(),curIns)!=curBBActualInsns->end())
                        continue;
                    errs()<<"\t"<<*curIns<<"\n";
                    if(curIns->mayReadOrWriteMemory())
                        continue;
                    bool found = false;
                    std::vector<Instruction*> seenBBs;
                    for(Value::use_iterator curUser = curIns->use_begin(), endUser = curIns->use_end();
                        curUser != endUser; ++curUser )
                    {
                        if(isa<Instruction>(*curUser))
                        {
                            found = localDescendentAccessMemory(cast<Instruction>(*curUser),seenBBs,insBBs);
                            if(found)
                                break;
                        }
                    }
                    if(found)
                    {

                        errs()<<" found =============------------------======================\n";
                        // curIns fanning out to some memory instruction
                        // lets search backward --- this can be using the instruction graph structure
                        //std::vector<Instruction*> duplicatedInstruction;

                        addDuplicatedInstruction(curIns,top->dagNodeMap,srcBBs,insBBs,top,this);


                        // TODO: we need to adjust the srcBBs coz now we have more insBBs

                    }


                }
            }
            errs()<<"end optimization\n";


        }
*/


    static void findDependentNodes(DAGNode* curNode, std::map<const Instruction *, DAGNode *> &nodeLookup,
                                   std::vector<DAGNode*> &depNodes)
    {
        for(unsigned int i=0; i< curNode->dagNodeContent.size(); i++)
        {
            InstructionGraphNode* curInsNode = curNode->dagNodeContent.at(i);
            for(InstructionGraphNode::iterator depIns = curInsNode->begin(), depInsE = curInsNode->end();
                depIns != depInsE; ++depIns)
            {
                Instruction* curDepIns = depIns->second->getInstruction();
                DAGNode* node2add = nodeLookup[curDepIns];
                depNodes.push_back(node2add);
            }
        }
    }




    void DecoupleInsScc::setControlFlowDuplication(bool cfDup)
    {
        controlFlowDuplication =cfDup;
    }
    bool DecoupleInsScc::DFSFindPartitionCycle(DAGPartition* dp)
    {
        if(dp->cycleDetectCovered)
            return true;
        dp->cycleDetectCovered = true;
        std::vector<DAGNode*>* curPartitionContent = &(dp->partitionContent);
        for(unsigned int ni = 0; ni < curPartitionContent->size();ni++)
        {
            DAGNode* curNode = curPartitionContent->at(ni);
            // for this curNode, we need to know its dependent nodes
            // then each of the dependent node will generate the next hop
            std::vector<DAGNode*> depNodes;
            findDependentNodes(curNode,dagNodeMap,depNodes);
            for(unsigned int di =0 ; di<depNodes.size(); di++)
            {
                DAGNode* nextNode=depNodes.at(di);
                std::vector<DAGPartition*>* nextHopPartitions = dagPartitionMap[nextNode];
                // the first partition this node is assigned to is the one used
                // in forming acyclic partition, later ones are duplicated nodes
                // which do not affect acyclicness of the pipeline
                DAGPartition* nextHop = nextHopPartitions->front();
                if(nextHop==dp)
                    continue;

                if(DFSFindPartitionCycle(nextHop))
                    return true;
            }
        }
        dp->cycleDetectCovered = false;
        return false;
    }


    bool DecoupleInsScc::generatePartition()
    {
        if(collectedDagNode.size()<=1)
            return false;
        // do a simple partition by cutting at the
        // boundary of memory access
        struct PartitionStrategies partitioner;
        partitioner.init(this);
        return partitioner.barrierPartition(collectedPartition);
    }
    void DecoupleInsScc::checkAcyclicDependency()
    {
        // lets check if there is any cycle between the partitions
        for(unsigned int pi = 0; pi < collectedPartition.size(); pi++)
        {
            DIS_MSG<<" partition #"<<pi<<"\n";
            DAGPartition* curPart = collectedPartition.at(pi);
            // dump partition content
            curPart->print();
            errs()<<"================\n";
            assert(!DFSFindPartitionCycle(curPart) && "cycle exist bewteen partitions" );
            /*if(DFSFindPartitionCycle(curPart))
            {
                errs()<<" cycle discovered quit\n";
                // now see which partitions are in the cycle
                for(unsigned int pie = 0; pie < collectedPartition.size(); pie++)
                {
                    DAGPartition* curParte = collectedPartition.at(pie);
                    if(curParte->cycleDetectCovered)
                    {
                        errs()<<"cycle contains "<<"\n";
                        curParte->print();
                        errs()<< "\n";
                    }
                }

                exit(1);
            }*/

        }
        errs()<<" no cycle discovered\n";
    }


    bool DecoupleInsScc::runOnFunction(Function &F)
    {
        if(F.hasFnAttribute(GENERATEDATTR))
            return false;
        errs() << "Try to decouple function: ";
        errs().write_escaped(F.getName()) << '\n';
        targetFunc = &F;
        bool seenReturnInst = false;
        int bbCount =0;
        std::set<std::string> usedBbNames;
        for(Function::iterator bbi = F.begin(), bbe = F.end(); bbi!=bbe; ++bbi)
        {
            if(bbi->getName().size()==0)
            {
                std::string bbPrefix("BB_Explicit_");
                std::string bbIndStr = boost::lexical_cast<std::string>(bbCount);
                std::string bbNameBase = bbPrefix+bbIndStr;
                std::string newBbName = bbNameBase;
                int vCountSuf = 0;
                while(usedBbNames.count(newBbName))
                {
                    newBbName = bbNameBase+"v"+boost::lexical_cast<std::string>(vCountSuf);
                    vCountSuf++;
                }
                bbi->setName(newBbName);
                usedBbNames.insert(newBbName);
                bbCount+=1;
            }
            else
            {
                std::string legal = bbi->getName();
                std::replace(legal.begin(),legal.end(),'.','_');
                bbi->setName(legal);
                usedBbNames.insert(legal);
            }
            BasicBlock* curBB = &(*bbi);
            if(isa<ReturnInst>(*(curBB->getTerminator())))
            {
                assert(!seenReturnInst && "multiple return statement in the llvm cfg,\
                            dppgen does not work with multiple return blocks\n \
                            There is a pass (Unify Function Exit nodes i.e.,-mergereturn \
                            <http://llvm.org/docs/Passes.html#mergereturn>) that transform \
                            a function to have only 1 return instruction.\n");

                seenReturnInst = true;
            }

        }
        InstructionGraphNode* rootNode = getAnalysis<InstructionGraph>().getRoot();
        // now we need to make the dag and create partition
        // each node in the dag is a set of instructions
        // we also have a mapping from each instruction to these sets
        for(scc_iterator<InstructionGraphNode*> curInsNScc = scc_begin(rootNode);
            curInsNScc != scc_end(rootNode); ++curInsNScc)
        {
            const std::vector<InstructionGraphNode*> &nodeCollection = *curInsNScc;
            if(nodeCollection.at(0)->getInstruction()!=0)
            {
                DAGNode* curDagNode = new DAGNode;
                curDagNode->init();
                curDagNode->dagNodeContent = nodeCollection;
                curDagNode->singleIns = (nodeCollection.size()==1);

                for (std::vector<InstructionGraphNode*>::const_iterator I = nodeCollection.begin(),
                         E = nodeCollection.end(); I != E; ++I)
                {
                    Instruction* curIns = (*I)->getInstruction();
                    curDagNode->sccLat += instructionLatencyLookup(curIns);
                    if(instructionExpensive(curIns))
                        curDagNode->expensive = true;
                    if(curIns->mayReadOrWriteMemory())
                    {
                        curDagNode->hasMemory = true;
                    }

                    DAGNode *&IGN = this->dagNodeMap[curIns];
                    IGN = curDagNode;
                }
                collectedDagNode.push_back(curDagNode);
            }
        }
        std::reverse(collectedDagNode.begin(),collectedDagNode.end());
        // now the dags are topologically sorted
        for(unsigned int dnInd =0; dnInd < collectedDagNode.size(); dnInd++)
        {
            DAGNode* curNode = collectedDagNode.at(dnInd);
            curNode->seqNum = dnInd;
        }

        int totalNumOfIns=0 ;
        for(unsigned int dnInd =0; dnInd < collectedDagNode.size(); dnInd++)
        {
            DAGNode* curNode = collectedDagNode.at(dnInd);
            curNode->print();
            errs()<<"\n============\n";
            totalNumOfIns+= curNode->dagNodeContent.size();
            std::vector<DAGNode*> myDep;
            findDependentNodes(curNode,this->dagNodeMap,myDep);
            // check every dep to make sure their seqNum is greater
            //errs()<<"my seq "<<curNode->seqNum<<" : my deps are ";
            for(unsigned depInd = 0; depInd<myDep.size(); depInd++)
            {
                //errs()<<myDep.at(depInd)->seqNum<<" ,";
                if(myDep.at(depInd)->seqNum < curNode->seqNum)
                {
                    errs()<<"not topologically sorted\n";
                    exit(1);

                }

            }

        }


        errs()<<"total number of instructions in scc nodes "<<totalNumOfIns<<"\n";
        errs()<<"all scc nodes topologically sorted, start partitioning \n";
        //----------------------------------
        // note the orignal function becomes
        // a single basic block with
        // whole bunch of function call
        //-----------------------------------
        bool change = generatePartition();
        if(change)
        {
            checkAcyclicDependency();
            std::vector<Function*> generatedFunctions;
            std::map<Instruction*,Value*> ins2AllocatedChannel;
            std::map<Function*,std::vector<Value*>*> ins2ArgList;
            for(unsigned k = 0; k<collectedPartition.size(); k++)
            {
                DAGPartition* curPartition = collectedPartition.at(k);
                // for each partition, we will be generating a whole new function
                // and insert it before the first instruction in the entry block
                // also we need to generate the argList when we generate the function
                // that is, we make a vector of values --- some of these are
                // the args of original, some are going to be just a place holder -- a pointer
                // to a allocaed data structure
                // we ll have a map of instruction --> vector of pointers (the first one being the producer)
                std::vector<Value*>* curFuncArgList = new std::vector<Value*>();
                // we will need to generate all the BBs for all the partitions
                // before we generate function for all of them, in case things get added
                curPartition->setupBBStructure();

                //Function* generatedFunc = curPartition->generateDecoupledFunction(k,ins2AllocatedChannel,curFuncArgList);
                //generatedFunctions.push_back(generatedFunc);
                //ins2ArgList[generatedFunc] = curFuncArgList;

            }
            BasicBlock* newSingleBB = BasicBlock::Create(this->targetFunc->getContext(),"newStreamTypeBB",this->targetFunc);

            newReplacementBBBuilder = new IRBuilder<>(newSingleBB);

            for(unsigned k = 0; k<collectedPartition.size(); k++)
            {
                DAGPartition* curPartition = collectedPartition.at(k);
                std::vector<Value*>* curFuncArgList = new std::vector<Value*>();
                Function* generatedFunc = curPartition->generateDecoupledFunction(k,ins2AllocatedChannel,curFuncArgList);
                generatedFunctions.push_back(generatedFunc);
                ins2ArgList[generatedFunc] = curFuncArgList;

            }


            //FIXME: the data structure for passing things around
            // need to be alloca'ed -- we first need to know
            // what are being passed around, this should be retrieved when we are creating functions
            //
            // so how do we do this? we will create a vector for each function
            // one for incoming value, one for out going value -- all corresponding to instructions
            // now we count how many needs to be communicated, and for each, how many outgoing
            // port are there, we can allocate a particular structure, it would be
            // consisted of one pointer connected to the producer and multiple pointers
            // connected to the consumers
            //


            //FIXME: now we have all the newly generated function
            // let's just delete what ever BB we originally have
            // and replace them with a straight line bb calling
            // each function in succession
            // do the alloca
            for(auto allocaIter = ins2AllocatedChannel.begin(); allocaIter!=ins2AllocatedChannel.end(); allocaIter++)
            {
                Value* actualAlloca = allocaIter->second;
                AllocaInst* curAllocaInst = &(cast<AllocaInst>(*actualAlloca));
                errs()<<"insert "<<*curAllocaInst<<"\n";
                //curAllocaInst->insertBefore(newSingleBB->begin());
            }
            errs()<<"done inserting alloca insts\n";

            unsigned correspondingPartitionInd = 0;
            CallInst* topReturn=0;
            for(auto funcPtrIter = generatedFunctions.begin(); funcPtrIter!=generatedFunctions.end(); funcPtrIter++)
            {
                std::vector<Value*>* curFuncArgList = ins2ArgList[*funcPtrIter];
                ArrayRef<Value*> argsVal(*curFuncArgList);
                errs()<<(*funcPtrIter)->getName()<<" function has been generated, now add callsite\n";
                CallInst* potentialRet = newReplacementBBBuilder->CreateCall(*funcPtrIter,argsVal);
                //CallInst::Create(*funcPtrIter,argsVal,(*funcPtrIter)->getName(),newSingleBB->end());
                delete curFuncArgList;
                if(collectedPartition.at(correspondingPartitionInd)->rInsn)
                {
                    errs()<<"function has return "<<(*funcPtrIter)->getName()<<"\n";
                    topReturn = potentialRet;
                }
                correspondingPartitionInd++;
            }
            errs()<<"adding return statement\n";
            if(topReturn ==0 ||topReturn->getType()->isVoidTy())
                newReplacementBBBuilder->CreateRetVoid();
            else
                newReplacementBBBuilder->CreateRet(topReturn);
            errs()<<"begin to remove all original BB\n";
            // final part, remove all the original BB
            if(change)
            {
                std::vector<BasicBlock*> toDelete;
                for(auto bbIter = targetFunc->begin(); bbIter!= targetFunc->end(); bbIter++)
                {
                    BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                    if(curBB!=newSingleBB)
                        toDelete.push_back(curBB);
                }
                for(auto bbPtrIter = toDelete.begin(); bbPtrIter!=toDelete.end(); bbPtrIter++)
                {

                    BasicBlock* toBeDeleted = *bbPtrIter;
                    errs()<<"to delete "<<toBeDeleted->getName()<<"\n";
                    //toBeDeleted->removeFromParent();;
                    errs()<<"not deleting \n";
                }
                targetFunc->addFnAttr(TRANSFORMEDATTR,"true");
            }
        }
        // clear up
        dagNodeMap.clear();
        dagPartitionMap.clear();
        // empty all the partitions
        for(unsigned k = 0; k<collectedPartition.size(); k++)
        {
            delete collectedPartition.at(k);
        }
        collectedPartition.clear();
        for(unsigned k =0; k<collectedDagNode.size();k++)
        {
            delete collectedDagNode.at(k);
        }
        getAnalysis<InstructionGraph>().releaseMemory();
        errs()<<"done main pass\n";

        BasicBlock* newlyAdded = newReplacementBBBuilder->GetInsertBlock();
        Function* curFunc = targetFunc;
        if(curFunc->hasFnAttribute(TRANSFORMEDATTR))
        {
            std::vector<BasicBlock*> DeadBlocks;
            for (Function::iterator I = targetFunc->begin(), E = targetFunc->end(); I != E; ++I)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*I));
                errs()<<curBB->getName()<<" is being processed\n";
                if(curBB == newlyAdded)
                {
                    errs()<<"no pred bb"<<curBB->getName()<<"\n";
                    errs()<<"do not touch\n";
                }
                else
                {
                    DeadBlocks.push_back(curBB);

                    /*while (PHINode *PN = dyn_cast<PHINode>(curBB->begin())) {
                      PN->replaceAllUsesWith(Constant::getNullValue(PN->getType()));
                      PN->dropAllReferences();
                      curBB->getInstList().pop_front();
                    }*/
                    //for (succ_iterator SI = succ_begin(curBB), E = succ_end(curBB); SI != E; ++SI)
                    //    (*SI)->removePredecessor(curBB);
                    for (BasicBlock::iterator II = curBB->begin(); II != curBB->end(); ++II) {
                        if(PHINode* PN = dyn_cast<PHINode>(II))
                            PN->replaceAllUsesWith(Constant::getNullValue(PN->getType()));
                        Instruction * insII = &(*II);
                        insII->dropAllReferences();
                    }

                    curBB->dropAllReferences();
                }
            }

            // Actually remove the blocks now.
            for (unsigned i = 0, e = DeadBlocks.size(); i != e; ++i) {
                errs()<<"erase "<<DeadBlocks[i]->getName()<<"\n";
                DeadBlocks[i]->eraseFromParent();

            }
        }


        return change;
    }
    void DecoupleInsScc::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<InstructionGraph>();
        AU.addRequired<DominatorTreeWrapperPass>();
        AU.addRequired<PostDominatorTree>();
        AU.addRequired<LoopInfo>();
        //AU.setPreservesAll();
    }
    std::vector<DAGPartition*>* DecoupleInsScc::getPartitionFromIns(Instruction* ins)
    {
        DAGNode* node = dagNodeMap[const_cast<Instruction*>(ins)];
        std::vector<DAGPartition*>* part = dagPartitionMap[const_cast<DAGNode*>(node)];
        return part;
    }
}

char DecoupleInsScc::ID = 0;
static RegisterPass<DecoupleInsScc> X("decoupleInsScc", "decoupleInsScc Pass");
FunctionPass *llvm::createDecoupleInsSccPass(bool cfDup)
{
    struct DecoupleInsScc* decouplingPass = new DecoupleInsScc();
    decouplingPass->setControlFlowDuplication(cfDup);
    return decouplingPass;
}
/*namespace {
  // Hello2 - The second implementation with getAnalysisUsage implemented.
  struct Hello2 : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Hello2() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HelloCounter;
      errs() << "Hello: ";
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };
}

char Hello2::ID = 0;
static RegisterPass<Hello2>
Y("hello2", "Hello World Pass (with getAnalysisUsage implemented)");
*/
