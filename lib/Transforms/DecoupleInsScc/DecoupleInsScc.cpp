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
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "generatePartitionsUtil.h"
#include <boost/lexical_cast.hpp>

#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "decoupleInsSCC"


namespace boost{
    void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        //throw e;
    }
}

namespace {

    struct DecoupleInsScc;

    struct DAGNode
    {
        std::vector<InstructionGraphNode*> dagNodeContent;
        bool singleIns;
        int sccLat;
        bool hasMemory;
        bool expensive;
        bool covered;
        int seqNum;
        void init()
        {
            singleIns = false;
            sccLat = 0;
            hasMemory = false;
            expensive = false;
            covered = false;
            seqNum = -1;
        }
        void print()
        {
            for(std::vector<InstructionGraphNode*>::iterator curInsNIter = dagNodeContent.begin();
                curInsNIter != dagNodeContent.end();
                curInsNIter++)
            {
                Instruction* curIns = (*curInsNIter)->getInstruction();
                errs()<<"\t"<<*curIns<<"\n";
            }
        }
    };

    struct DAGPartition
    {

        std::vector<DAGNode*> partitionContent;

        bool cycleDetectCovered;

        /*******************properties of this particular partition **************/
        bool containMemory;
        bool containLongLatCyc;
        struct DecoupleInsScc* top;


        /******************data structure facilitating transformations ***********/
        // return instruction if it is here, 0 if it is not
        Instruction* rInsn;
        // since the partition only contains a subset of basic blocks
        // we can potentially skip basic blocks:
        // e.g. in the original cfg: A->B->C, and only A and C are in
        // in partition, we have a map which maps B to C
        std::map<BasicBlock*,BasicBlock*> partitionBranchRemap;
        std::vector<BasicBlock*> singleSucBBs;
        // the dominator for all basicblocks in this partition
        // the entry block for this partition will be here
        BasicBlock* dominator;
        void init(struct DecoupleInsScc* tt )
        {
            containMemory = false;
            containLongLatCyc = false;
            cycleDetectCovered = false;
            top = tt;
            rInsn= NULL;
            dominator=0;
        }
        void print()
        {
            for(unsigned int nodeI = 0; nodeI < partitionContent.size(); nodeI++)
            {
                DAGNode* curNode = partitionContent.at(nodeI);
                curNode->print();
                errs()<<"\n";
            }
        }
        void addDagNode(DAGNode* dagNode,DagNode2PartitionMap &nodeToPartitionMap )
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
            if(nodeToPartitionMap.find(dagNode) == nodeToPartitionMap.end())
            {
                listOfDp = new std::vector<DAGPartition*>();
                nodeToPartitionMap[dagNode] = listOfDp;
            }
            else
                listOfDp = nodeToPartitionMap[dagNode];
            listOfDp->push_back(this);
        }
            void trimBBList()
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
                // and nRBB is added to the keeper queue,

                std::vector<BasicBlock*>* allBBs = (top->allBBsInPartition)[this];
                BBMap2Ins*  srcBBs = (top->srcBBsInPartition)[this];
                BBMap2Ins*  insBBs = (top->insBBsInPartition)[this];
                std::vector<BasicBlock*> allRealBBs;
                for(BBMapIter bmi = srcBBs->begin(), bme = srcBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    allRealBBs.push_back(curBB);

                }
                for(BBMapIter bmi = insBBs->begin(), bme = insBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    if(srcBBs->find(curBB)==srcBBs->end()) // if curBB was not in srcBB
                        allRealBBs.push_back(curBB);

                    // now if this is generating a result based on incoming edges
                    // then the basic block incoming edge should be counted as real
                    // and always be preserved.
                    std::vector<Instruction*>* curBBInsns = (*insBBs)[curBB];
                    addPhiOwner2Vector(curBBInsns, &allRealBBs);

                }
                std::vector<BasicBlock*> toRemove = (*allBBs);


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
                    std::vector<BasicBlock*> seenBBs;
                    seenBBs.push_back(curSeed);

                    if(predMap->find(curSeed)!=predMap->end())
                    {
                        std::vector<BasicBlock*>* curPreds = (*predMap)[curSeed];

                        for(unsigned int cPred = 0; cPred < curPreds->size(); cPred++)
                        {
                            BasicBlock* curPred = curPreds->at(cPred);
                            searchToFindKeeper(curSeed, curPred,predMap, toKeep, allKeepers, seenBBs,PDT, allBBs );
                        }
                    }

                }

                // we now try to build the remap
                // we start from one keeper, look for the next keeper in dfs way
                // realize every keeper is a divergent point
                // from a outgoing edge of an keeper, there can be only one keeper
                //this->partitionBranchRemap
                //errs()<<"======keeper=====\n";
                for(unsigned int keeperInd = 0; keeperInd < allKeepers.size();keeperInd++)
                {
                    BasicBlock* curKeeper = allKeepers.at(keeperInd);
                    //errs()<<curKeeper->getName()<<" ";
                    TerminatorInst* termIns = curKeeper->getTerminator();
                    int numOutEdge = termIns->getNumSuccessors();

                    for(unsigned int brInd = 0; brInd<numOutEdge; brInd++)
                    {
                        std::vector<BasicBlock*> curBranchKeeper;
                        // from this edge, we find next keeper
                        BasicBlock* brSuccessor = termIns->getSuccessor(brInd);
                        std::vector<BasicBlock*> seenBBs;
                        search4NextKeeper( brSuccessor, allKeepers, curBranchKeeper, seenBBs, *allBBs );

                        if(curBranchKeeper.size()>1)
                        {
                            errs()<<"more than one dest\n";
                            exit(1);
                        }
                        else if(curBranchKeeper.size()==0)
                        {
                            // this is an error coz if a keeper is branching somwhere but
                            // got nothing, something is wrong
                            errs()<<"no dest from keeper\n";
                            exit(1);
                        }
                        else
                        {
                            this->partitionBranchRemap[brSuccessor] = curBranchKeeper.at(0);
                        }

                    }
                    // now remove this keeper from toRemove
                    std::vector<BasicBlock*>::iterator rmKeeperIter = std::find(toRemove.begin(),toRemove.end(),curKeeper);
                    if(rmKeeperIter==toRemove.end())
                    {
                        errs()<<"somehow not found\n";
                        exit(1);
                    }
                        // must be found
                    toRemove.erase(rmKeeperIter);

                }
                // now actually remove it
                for(unsigned int trind = 0; trind < toRemove.size(); trind++)
                {
                    BasicBlock* bb2Remove = toRemove.at(trind);
                    std::vector<BasicBlock*>::iterator found = std::find(allBBs->begin(),allBBs->end(),bb2Remove);
                    errs()<<(*found)->getName()<<" removed " <<"\n";
                    allBBs->erase(found);
                }
                // we shall build a map of basicblocks who now only have
                // one successor -- meaning they do not need to get remote branchtag
                // traverse every block, check their destination (with remap)
                // if it is a single

                for(unsigned int allBBInd=0; allBBInd<allBBs->size(); allBBInd++)
                {
                    BasicBlock* curBB = allBBs->at(allBBInd);
                    TerminatorInst* curTermInst = curBB->getTerminator();
                    if(!isa<ReturnInst>(*curTermInst))
                    {
                        int numSuc = curTermInst->getNumSuccessors();
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
                            singleSucBBs.push_back(curBB);
                    }
                }




            }
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

            void generateBBList()
            {
                // at this point instructions are associated with the partition
                // each of these instructions' owner bb is of course relevant to
                // to the partition
                // each of these instructions are getting operand from somewhere:
                // either instruction or function arg, if the source is an instruction I_s
                // then I_s' owner basic block would be relevant for this partition as well
                // each map keeps track of the BBs and the contained relevant instructions
                BBMap2Ins* sourceBBs = new BBMap2Ins;
                BBMap2Ins* insBBs = new BBMap2Ins;

                //std::vector<Instruction*> srcInstructions;
                for(unsigned int nodeInd = 0; nodeInd < partitionContent.size(); nodeInd++)
                {
                    DAGNode* curNode = partitionContent.at(nodeInd);
                    for(unsigned int insInd = 0; insInd< curNode->dagNodeContent->size(); insInd++)
                    {
                        Instruction* curIns = curNode->dagNodeContent->at(insInd)->getInstruction();
                        if(isa<ReturnInst>(*curIns))
                            rInsn= curIns;
                        addBBInsToMap(*insBBs,curIns);
                        /*unsigned int numOp = curIns->getNumOperands();
                        for(unsigned int opInd = 0; opInd < numOp; opInd++)
                        {
                            Value* curOp = curIns->getOperand(opInd);
                            if(isa<Instruction>(*curOp))
                            {
                                Instruction* srcIns = &(cast<Instruction>(*curOp));
                                //srcInstructions.push_back(srcIns);
                                addBBInsToMap(*sourceBBs,srcIns);
                            }
                        }*/
                        addSrcIns(curIns,sourceBBs);
                    }
                }
                optimizeBBByDup(sourceBBs,insBBs);
                // dominator for src and ins BBs
                // this will be the first basicblock we need to convert
                // then we shall see where each bb diverge?

                DominatorTree* DT= top->getAnalysisIfAvailable<DominatorTree>();
                dominator = findDominator(dominator,sourceBBs,DT);
                dominator = findDominator(dominator,insBBs,DT);

                BasicBlock* postDominator =0;
                PostDominatorTree* PDT = top->getAnalysisIfAvailable<PostDominatorTree>();
                postDominator = findPostDominator(postDominator,sourceBBs,PDT);
                postDominator = findPostDominator(postDominator,insBBs,PDT);

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
                std::vector<BasicBlock*>* AllBBs = new std::vector<BasicBlock*>();
                // some of these BBs wont be in the map, in which case, we just need the terminator
                AllBBs->push_back(dominator);

                // add the path from the dominator to all the sourceBBs
                addPathBBsToBBMap(*sourceBBs,dominator,*AllBBs,dominator);
                addPathBBsToBBMap(*insBBs,dominator,*AllBBs,dominator);
                // now  we do the n^2 check to see if anybody goes to anybody else?
                for(BBMapIter bmi = sourceBBs->begin(), bme = sourceBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    //std::vector<BasicBlock*> curPathStorage;
                    addPathBBsToBBMap(*sourceBBs,curBB,*AllBBs,dominator);
                    addPathBBsToBBMap(*insBBs,curBB,*AllBBs,dominator);
                    //searchToGroupAndAdd(curBB,dominator,*sourceBBs,curPathStorage,*AllBBs);
                    // same thing for insBB
                    //searchToGroupAndAdd(curBB,dominator,*insBBs,curPathStorage,*AllBBs);

                }
                for(BBMapIter bmi = insBBs->begin(), bme = insBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    //std::vector<BasicBlock*> curPathStorage;
                    addPathBBsToBBMap(*sourceBBs,curBB,*AllBBs,dominator);
                    addPathBBsToBBMap(*insBBs,curBB,*AllBBs,dominator);
                    //searchToGroupAndAdd(curBB,dominator,*sourceBBs,curPathStorage,*AllBBs);
                    // same thing for insBB
                    //searchToGroupAndAdd(curBB,dominator,*insBBs,curPathStorage,*AllBBs);

                    // now we shall look at the phi
                    std::vector<Instruction*>* curBBInsns = bmi->second;
                    addPhiOwner2Vector(curBBInsns, AllBBs);

                }

                // a special pass to search for every insBB and srcBB themselves
                // FIXME: because our search finishes when we see the destination
                // there is a case where we will generate wrong graph:
                // a BB (BB1) fans out to some other BBs then those BBs loop back to BB1
                // BB1 to those BBs and back will not be part of our control graph
                // but they should
                // this is fixed below as we start searchinf from every block's
                // successor
                for(BBMapIter bmi = insBBs->begin(), bme = insBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    // search all its successor
                    addPathToSelf(curBB,*AllBBs,dominator);
                }
                for(BBMapIter bmi = sourceBBs->begin(), bme = sourceBBs->end(); bmi!=bme; ++bmi)
                {
                    BasicBlock* curBB=bmi->first;
                    addPathToSelf(curBB,*AllBBs,dominator);
                }



                LoopInfo* li =top->getAnalysisIfAvailable<LoopInfo>();
                if(!top->NoCfDup)
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


                        //int sccNum=0;
                        std::vector<BasicBlock*> cpAllBBs = *AllBBs;
                        for(unsigned int existingBBInd = 0; existingBBInd < cpAllBBs.size(); existingBBInd++)
                        {
                            BasicBlock* curExistingBB = cpAllBBs.at(existingBBInd);
                            for (scc_iterator<Function*> SCCI = scc_begin(top->curFunc),
                                   E = scc_end(top->curFunc); SCCI != E; ++SCCI)
                            {
                              std::vector<BasicBlock*> &nextSCC = *SCCI;
                              if(std::find(nextSCC.begin(),nextSCC.end(),curExistingBB)!=nextSCC.end())
                              {
                                  // this scc should be included
                                  mergeInto(nextSCC,*AllBBs);
                              }
                            }
                        }
                        dominator = findDominator(dominator,AllBBs,DT);

                    }

                }

                // now all the basicblocks are added into the allBBs
                (top->srcBBsInPartition)[this]=sourceBBs;
                (top->insBBsInPartition)[this]=insBBs;
                (top->allBBsInPartition)[this]=AllBBs;


                // go to release memory here

            }




        };

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



    struct DecoupleInsScc : public FunctionPass {
        static char ID; // Pass identification, replacement for typeid
        bool controlFlowDuplication;
        std::vector<DAGNode*> collectedDagNode;
        std::map<const Instruction *, DAGNode *> dagNodeMap;
        DecoupleInsScc() : FunctionPass(ID) {

        }
        void setControlFlowDuplication(bool cfDup)
        {
            controlFlowDuplication =cfDup;
        }

        bool runOnFunction(Function &F) override {
            errs() << "Try to decouple function: ";
            errs().write_escaped(F.getName()) << '\n';
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
                    std::string tmpBbName = bbNameBase;
                    int vCountSuf = 0;
                    while(usedBbNames.count(tmpBbName))
                    {
                        tmpBbName = bbNameBase+"v"+boost::lexical_cast<std::string>(vCountSuf);
                        vCountSuf++;
                    }
                    std::string newBbName = tmpBbName;
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
            errs()<<"all scc nodes topologically sorted, continue\n";
            //----------------------------------
            // note the orignal function becomes
            // a single basic block with
            // whole bunch of function call
            //-----------------------------------
            //

            return true;
        }
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequiredTransitive<InstructionGraph>();
            AU.addRequiredTransitive<DominatorTreeWrapperPass>();
            AU.addRequired<PostDominatorTree>();
            AU.addRequired<LoopInfo>();
            AU.setPreservesAll();
        }
    };
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
