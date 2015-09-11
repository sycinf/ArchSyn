#ifndef GENERATEPARTITIONSUTIL_H
#define GENERATEPARTITIONSUTIL_H

#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

using namespace llvm;
struct DAGNode;
struct DAGPartition;


typedef std::map<DAGNode*, std::vector<DAGPartition*>*> DagNode2PartitionMap;
typedef std::map<BasicBlock*, std::vector<Instruction*>*> BBMap2Ins;
typedef BBMap2Ins::iterator BBMapIter;

static BasicBlock* findDominator(BasicBlock* originalDominator,std::set<BasicBlock*>& allBBs, DominatorTree* DT)
{
    BasicBlock* dominator = originalDominator;
    for(auto bbIter = allBBs.begin(); bbIter!=allBBs.end(); bbIter++)
    {
        BasicBlock* curBB = *bbIter;
        if(dominator!=0)
            dominator = DT->findNearestCommonDominator(dominator, curBB);
        else
            dominator = curBB;
    }
    return dominator;
}
static BasicBlock* findDominator(BasicBlock* originalDominator,BBMap2Ins& mapOfBBs,DominatorTree* DT )
{
   std::set<BasicBlock*> allBBs;
   for(BBMapIter bmi = mapOfBBs.begin(), bme = mapOfBBs.end(); bmi!=bme; ++bmi)
   {
       BasicBlock* curBB=bmi->first;
       allBBs.insert(curBB);
   }
   return findDominator(originalDominator,allBBs,DT);
}

static BasicBlock* findPostDominator(BasicBlock* originalPostDominator,BBMap2Ins* mapOfBBs,PostDominatorTree* PDT )
{
   BasicBlock* postDominator = originalPostDominator;
   for(BBMapIter bmi = mapOfBBs->begin(), bme = mapOfBBs->end(); bmi!=bme; ++bmi)
   {
       BasicBlock* curBB=bmi->first;
       if(postDominator!=0)
       {
           postDominator = PDT->findNearestCommonDominator(postDominator, curBB);

       }
       else
           postDominator = curBB;
   }
   return postDominator;
}


static void addBBInsToMap(BBMap2Ins& map, Instruction* curIns)
{
    BasicBlock* bbKey = curIns->getParent();
    BBMapIter I = map.find(bbKey);
    if(I== map.end())
    {
        std::vector<Instruction*>* curBBIns = new std::vector<Instruction*>();
        curBBIns->push_back(curIns);
        map[bbKey] = curBBIns;
    }
    else
    {
        std::vector<Instruction*>*& curBBIns = map[bbKey];
        // now check if it is already included
        if(std::find(curBBIns->begin(),curBBIns->end(),curIns )== curBBIns->end())
        {
            curBBIns->push_back(curIns);
        }
    }
}

static void findAllSrcIns(Instruction* actualIns, std::set<Instruction*>& srcInsns)
{
    unsigned int numOp = actualIns->getNumOperands();
    for(unsigned int opInd = 0; opInd < numOp; opInd++)
    {
        Value* curOp = actualIns->getOperand(opInd);
        if(isa<Instruction>(*curOp))
        {
            assert(!isa<TerminatorInst>(*curOp) && "source instruction is a terminator!");
            Instruction* srcIns = &(cast<Instruction>(*curOp));
            srcInsns.insert(srcIns);
        }
    }
}

static void addSrcIns(Instruction* actualIns, BBMap2Ins& sourceBBs)
{
    std::set<Instruction*> allSrcIns;
    findAllSrcIns(actualIns,allSrcIns);
    for(unsigned int insInd = 0; insInd < allSrcIns.size(); insInd++)
    for(auto srcInsIter = allSrcIns.begin(); srcInsIter != allSrcIns.end(); srcInsIter++)
    {
        Instruction* srcIns = *srcInsIter;
        addBBInsToMap(sourceBBs,srcIns);
    }
}

static void mergeInto(const std::vector<BasicBlock*>& small, std::set<BasicBlock*>& big)
{
    //errs()<<"into merge";
    for(auto bbIter = small.begin(); bbIter!= small.end(); bbIter++)
    {
       BasicBlock* curPathBB = *bbIter;
       big.insert(curPathBB);
    }
//errs()<<" outo merge";
}

static bool searchToBasicBlock(std::vector<BasicBlock*>& storage,
                               BasicBlock* current, BasicBlock* target, BasicBlock* domInter )
{
    //errs()<<" search to bb starting "<<current->getName()<<" towards "<<target->getName()<<"\n";
    storage.push_back(current);
    if(current == target)
    {
        return true;
    }
    bool keepCurrent = false;
    for(unsigned int ind = 0; ind < current->getTerminator()->getNumSuccessors(); ind++)
    {
        BasicBlock* curSuc = current->getTerminator()->getSuccessor(ind);
        if(std::find(storage.begin(),storage.end(),curSuc) != storage.end())
        {
            //curSuc already in the array, try the next one
            continue;
        }
        // if this path goes through dominator then its disregarded
        if(curSuc == domInter)
            continue;
        bool found = searchToBasicBlock(storage, curSuc, target,domInter);
        if(found)
        {
            //storage.push_back(curSuc);
            keepCurrent = true;
        }
    }
    if(!keepCurrent)
        storage.pop_back();
    return keepCurrent;
}

static void mergePath2BBIntoStorage(BasicBlock* src,BasicBlock* dest, BasicBlock* domInter,
                                    std::set<BasicBlock*>& storage)
{
    std::vector<BasicBlock*> tmpStorage;
    if(searchToBasicBlock(tmpStorage,src,dest,domInter))
        mergeInto(tmpStorage,storage);
}

static void addPathBBsToBBMap(BBMap2Ins& dstBBs, BasicBlock* startBB, std::set<BasicBlock*>& AllBBs,BasicBlock* domInter)
{
    for(BBMapIter bmi = dstBBs.begin(), bme = dstBBs.end(); bmi!=bme; ++bmi)
    {
        BasicBlock* dest=bmi->first;
        mergePath2BBIntoStorage(startBB,dest,domInter,AllBBs);
    }
}
static void addPhiOwner2Vector(std::vector<Instruction*>* curBBInsns, std::set<BasicBlock*>& add2Vector)
{
    for(unsigned int insInd = 0; insInd < curBBInsns->size(); insInd++)
    {
        Instruction* curIns = curBBInsns->at(insInd);
        if(isa<PHINode>(*curIns))
        {
            PHINode* curPhiIns = (PHINode*)(curIns);
            for(unsigned int inBlockInd = 0; inBlockInd<curPhiIns->getNumIncomingValues();inBlockInd++)
            {
                BasicBlock* curPred = curPhiIns->getIncomingBlock(inBlockInd);
                add2Vector.insert(curPred);
            }
        }
    }
}
static void addPathToSelf(BasicBlock* curBB,std::set<BasicBlock*>& AllBBs, BasicBlock* dominator)
{
    int numSuc = curBB->getTerminator()->getNumSuccessors();
    for(int sucInd = 0; sucInd<numSuc; sucInd++)
    {
        BasicBlock* startBB=curBB->getTerminator()->getSuccessor(sucInd);
        mergePath2BBIntoStorage(startBB,curBB,dominator,AllBBs);
    }
}

static void searchToFindKeeper(BasicBlock* curSeed, BasicBlock* curPred, BB2BBVectorMapTy* predMap,
                               std::vector<BasicBlock*>& toKeep, std::vector<BasicBlock*>& allKeepers,
                               std::vector<BasicBlock*>& seenBBs, PostDominatorTree* PDT, std::set<BasicBlock*>& allBBs )
{
    if(std::find(seenBBs.begin(), seenBBs.end(),curPred)!=seenBBs.end())
    {
        //already seen
        return;
    }
    // if not even in the partition, we dont care
    if(!allBBs.count(curPred))
        return;

    // if it is already a keeper, then we stop
    if(std::find(toKeep.begin(),toKeep.end(),curPred)!=toKeep.end()  ||
            std::find(allKeepers.begin(),allKeepers.end(),curPred)!=allKeepers.end())
    {
        return;
    }

    if(! PDT->dominates(curSeed,curPred))
    {
        // this is a keeper
        toKeep.push_back(curPred);
        return;
    }
    else
    {
        // not a keeper, we continue
        seenBBs.push_back(curPred);
        if(predMap->find(curPred)!=predMap->end())
        {
            std::vector<BasicBlock*>* nextPreds = (*predMap)[curPred];

            for(unsigned int cPred = 0; cPred < nextPreds->size(); cPred++)
            {
                BasicBlock* nextPred = nextPreds->at(cPred);
                searchToFindKeeper(curSeed, nextPred,predMap, toKeep, allKeepers, seenBBs,PDT,allBBs );
            }
        }
    }
}
static void search4NextKeeper(BasicBlock* brSuccessor, std::vector<BasicBlock*>&allKeepers, std::vector<BasicBlock*>&curBranchKeeper,
                              std::vector<BasicBlock*>&seenBBs, std::set<BasicBlock*>&allBBs )
{
    // seen it before, we return
    if(std::find(seenBBs.begin(),seenBBs.end(), brSuccessor)!=seenBBs.end())
        return;
    seenBBs.push_back(brSuccessor);
    // if this is outside current partition, its sort of a keeper...
    bool toAdd = false;
    if(!allBBs.count(brSuccessor))
    {
        toAdd = true;
    }
    else if(std::find(allKeepers.begin(),allKeepers.end(),brSuccessor)!=allKeepers.end())
    {
        // we found a keeper
        toAdd = true;
    }

    if(toAdd)
    {
        if(std::find(curBranchKeeper.begin() ,curBranchKeeper.end() ,brSuccessor)==curBranchKeeper.end())
        {
            curBranchKeeper.push_back(brSuccessor);
        }
    }
    else
    {
        // start next hop
        TerminatorInst* termIns = brSuccessor->getTerminator();
        int numOutEdge = termIns->getNumSuccessors();
        // this is going nowhere, so it must be a keeper right?
        // lets say if it is not a keeper, it is definitely a flow only block
        // why would a bb with no successor be a flow only block? it cant
        // so it is not true -- this cannot happen
        assert(numOutEdge!=0 && "a terminator with no successor ");

        for(unsigned int brInd = 0; brInd<numOutEdge; brInd++)
        {
            BasicBlock* nextSuccessor = termIns->getSuccessor(brInd);
            search4NextKeeper( nextSuccessor, allKeepers, curBranchKeeper, seenBBs, allBBs );
        }

    }
}

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
