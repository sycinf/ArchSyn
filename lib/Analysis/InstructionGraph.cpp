#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
using namespace llvm;
static void addAncestorsNotPDominatedBy(BasicBlock* tgtBB, std::vector<BasicBlock*>* curPredecessors,
                                        PostDominatorTree* PDT, BB2BBVectorMapTy& BasicBlock2Predecessors,
                                        std::vector<BasicBlock*>* allGoodPredicates, std::vector<BasicBlock*>& seenBBs,LoopInfo* li)
{
    //errs()<<"get tgtBB's earliest" <<tgtBB->getName()<<"\n";
    for(unsigned int predInd=0; predInd < curPredecessors->size(); predInd++ )
    {
        BasicBlock* BB = curPredecessors->at(predInd);
        // if this block is already part of the known predicate or it has been examined before
        if(std::find(allGoodPredicates->begin(),allGoodPredicates->end(),BB)!=allGoodPredicates->end() ||
           std::find(seenBBs.begin(),seenBBs.end(),BB)!=seenBBs.end()
                )
            continue;
        seenBBs.push_back(BB);
        std::vector<BasicBlock*>* directProcessor = BasicBlock2Predecessors[tgtBB];
        // also if the predicate is in an outer loop compared to the curBB, and
        // it is not a direct predecessor, we skip it
        if(std::find(directProcessor->begin(),directProcessor->end(),BB)==directProcessor->end()&&li->getLoopDepth(tgtBB) > li->getLoopDepth(BB))
            continue;

        // note: a basic block does not properly dominates itself
        if(!PDT->properlyDominates(tgtBB,BB)  || (BasicBlock2Predecessors.find(BB)==BasicBlock2Predecessors.end()))
        {
            // add BB to the allGoodPredicates, and end this search path
            allGoodPredicates->push_back(BB);
        }
        // tgtBB is inner loop, but BB is direct predecessor, we make it predicate
        // we are not propogating too far out of the loop
        else if(std::find(directProcessor->begin(),directProcessor->end(),BB)!=directProcessor->end()&&li->getLoopDepth(tgtBB) > li->getLoopDepth(BB))
        {
            allGoodPredicates->push_back(BB);
        }
        // we still have ancestors
        else
        {
            std::vector<BasicBlock*>* nextStepPred = BasicBlock2Predecessors[BB];
            addAncestorsNotPDominatedBy(tgtBB,nextStepPred,PDT,BasicBlock2Predecessors,allGoodPredicates,seenBBs,li);
        }
    }

}


static std::vector<BasicBlock*>* searchEarliestPredicate(BasicBlock* curBB, PostDominatorTree* PDT, BB2BBVectorMapTy& BasicBlock2Predecessors,LoopInfo* li)
{
    // do a search backward till a basicblock who is not post dominated by the currentBB, then we add it to the vector
    // let's assume this vector wont be too big...

    std::vector<BasicBlock*>* allGoodPredicates = new std::vector<BasicBlock*>();
    std::vector<BasicBlock*>* curPredecessors = BasicBlock2Predecessors[curBB];
    std::vector<BasicBlock*> seenBBs;

    addAncestorsNotPDominatedBy(curBB, curPredecessors,PDT, BasicBlock2Predecessors, allGoodPredicates, seenBBs, li);
    return allGoodPredicates;
}


InstructionGraph::InstructionGraph()
    : FunctionPass(ID), Root(0), ExternalInsNode(new InstructionGraphNode(0)) {
  initializeInstructionGraphPass(*PassRegistry::getPassRegistry());
}

void InstructionGraph::addToInstructionGraph(Instruction *I, std::vector<BasicBlock*>* earliestPred) {
    InstructionGraphNode *Node = getOrInsertInstruction(I);
    // If this function has external linkage, anything could call it.
    //if (!F->hasLocalLinkage()) {
    //  ExternalCallingNode->addCalledFunction(CallSite(), Node);

    // for all the instructions, we have the external instruction as
    // the ancestor
    ExternalInsNode->addDependentInstruction(Node);



    // Look for dependent instruction
    for(Value::user_iterator curUser = I->user_begin(), endUser = I->user_end(); curUser != endUser; ++curUser )
    {
        if(!isa<Instruction>(*curUser))
        {
            errs()<<"instruction used by non instruction\n";
            exit(1);
        }
        else
        {

            Node->addDependentInstruction(getOrInsertInstruction( cast<Instruction>(*curUser)));

        }
      //Node->addDependentInstruction(getOrInsertInstruction(curIns));
    }
    if(earliestPred!=0)
    {
        for(unsigned int predInd = 0; predInd < earliestPred->size(); predInd++ )
        {
            BasicBlock* predBB = earliestPred->at(predInd);
            TerminatorInst* predBBTermInst = predBB->getTerminator();
            InstructionGraphNode* predBBTermNode = getOrInsertInstruction(predBBTermInst);
            predBBTermNode->addDependentInstruction(Node);
        }
    }


/*
  if(isa<TerminatorInst>(*I))
  {
      // the terminator inst of its successors are dependent on it
      unsigned numSuc = cast<TerminatorInst>(*I).getNumSuccessors();
      for(unsigned int sucInd=0; sucInd<numSuc; sucInd++)
      {
          BasicBlock* curSuc = cast<TerminatorInst>(*I).getSuccessor(sucInd);
          // try have everybody in the bb being control dependent
          // to this node
          for(BasicBlock::iterator BI = curSuc->begin(), BE = curSuc->end(); BI != BE; ++BI)
          {
               //addToInstructionGraph(BI);
               Node->addDependentInstruction(getOrInsertInstruction(BI));
          }
          //Instruction* endOfSuc = curSuc->getTerminator();
           //Node->addDependentInstruction(getOrInsertInstruction(endOfSuc));
      }
  }
*/


    // we need to look at other kinda dependence --- memory?
    if(I->mayReadFromMemory())
    {
    //look at all other instructions who read the same memory segment
    }
    if(I->mayWriteToMemory())
    {

    }

}

void InstructionGraph::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredTransitive<DominatorTreeWrapperPass>();
    AU.addRequiredTransitive<PostDominatorTree>();
    AU.addRequiredTransitive<LoopInfo>();
    AU.setPreservesAll();
}




bool InstructionGraph::runOnFunction(Function &M) {
    errs()<<"ins grph ";

    // if the function dppcreated, then we skip
    if(M.hasFnAttribute("dppcreated"))
        return false;
    errs()<<"skip check";
    Func = &M;

    ExternalInsNode = getOrInsertInstruction(0);
    //assert(!CallsExternalNode);
    //CallsExternalNode = new CallGraphNode(0);
    Root = ExternalInsNode;
    for(Function::iterator BB=M.begin(), BBE = M.end(); BB != BBE; ++BB)
    {
        TerminatorInst* curBBTerm =  BB->getTerminator();
        unsigned numSuc = curBBTerm->getNumSuccessors();
        for(unsigned sucInd=0; sucInd < numSuc; sucInd++)
        {
            BasicBlock* curSuc = curBBTerm->getSuccessor(sucInd);
            std::vector<BasicBlock*>* curSucPredecessors;
            if(BasicBlock2Predecessors.find(curSuc)==BasicBlock2Predecessors.end())
            {
                curSucPredecessors = new std::vector<BasicBlock*>();
                BasicBlock2Predecessors[curSuc] = curSucPredecessors;
            }
            else
                curSucPredecessors = BasicBlock2Predecessors[curSuc];
            curSucPredecessors->push_back(&(*BB));
        }
    }
    PostDominatorTree* PDT = getAnalysisIfAvailable<PostDominatorTree>();
    LoopInfo* LI = getAnalysisIfAvailable<LoopInfo>();
    for (Function::iterator BB = M.begin(), BBE = M.end(); BB != BBE; ++BB)
    {
        BasicBlock* curBBPtr = &(*BB);
        std::vector<BasicBlock*>* earliestPred=0;
        if(BasicBlock2Predecessors.find(curBBPtr)==BasicBlock2Predecessors.end())
        {
            assert (&(M.getEntryBlock()) == curBBPtr);
        }
        else
        {
          // now we shall search for the actual predicate instruction
            earliestPred = searchEarliestPredicate(curBBPtr, PDT, BasicBlock2Predecessors,LI);
        }
        /*errs()<<"bb:"<<BB->getName() << " has "<<" pred \n";
        for(unsigned k = 0;k < earliestPred->size(); k++)
        {
          errs()<< earliestPred->at(k)->getName()<<"\n";
        }*/

        for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
        {
           addToInstructionGraph(BI, earliestPred);
        }
        delete earliestPred;
        errs()<<BB->getName()<<"\t" <<ExternalInsNode->DependentInstructions.size()<<" root dep\n";
    }
    // we should have all the node
    errs()<<InstructionMap.size()<< " instructions added as graph nodes\n";

    return false;
}

INITIALIZE_PASS(InstructionGraph, "basicig", "InstructionGraph Construction", false, true)

char InstructionGraph::ID = 0;
void InstructionGraph::releaseMemory() {
  /// CallsExternalNode is not in the function map, delete it explicitly.

  if (InstructionMap.empty())
    return;

  for (InstructionMapTy::iterator I = InstructionMap.begin(), E = InstructionMap.end();
      I != E; ++I)
    delete I->second;
  InstructionMap.clear();
}


void InstructionGraph::print(raw_ostream &OS, const Module*) const {

  for (InstructionGraph::const_iterator I = begin(), E = end(); I != E; ++I)
    I->second->print(OS);
}
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void InstructionGraph::dump() const {
  print(dbgs(), 0);
}
#endif

InstructionGraphNode *InstructionGraph::getOrInsertInstruction(const Instruction *F) {
  InstructionGraphNode *&IGN = InstructionMap[F];
  if (IGN) return IGN;
 /* if(F)
  {
      errs()<<*F<<"\n";
      errs()<<F->getParent()->getParent()->getName()<<"\n";
      errs()<<Func->getName()<<"\n";
      errs()<<Func<<"   "<<F->getParent()->getParent()<<"\n";
  }*/
  assert((!F || F->getParent()->getParent()== Func) && "instruction not in current function!");
  return IGN = new InstructionGraphNode(const_cast<Instruction*>(F));
}

void InstructionGraphNode::print(raw_ostream &OS) const {
  if (Instruction *myI = getInstruction())
    OS << *myI << "'";
  else
    OS << "external pesudo ins";

  OS << "<<" << this << ">>  #uses=" << DependentInstructions.size() << '\n';

  for (const_iterator I = begin(), E = end(); I != E; ++I) {
      OS << "  <" << *(I->second->getInstruction()) << "> \n ";
  }
  OS << '\n';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void InstructionGraphNode::dump() const { print(dbgs()); }
#endif
