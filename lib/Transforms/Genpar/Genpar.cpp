
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/GenPar/Genpar.h"
#include "llvm/Transforms/BoostException.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/InstrTypes.h"
#include <set>
#include <boost/lexical_cast.hpp>
using namespace llvm;


/*namespace boost{
    void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        exit(1);
    }
}*/

namespace llvm{


    struct Genpar : public FunctionPass {
        static char ID;
        llvm::raw_ostream* out_c;
        bool printDA;
        Genpar() : FunctionPass(ID) {}
        Genpar(llvm::raw_ostream& OS, bool DAInfo) : FunctionPass(ID){
            out_c= &OS;
            printDA = DAInfo;
        }


        void cloneLoop(Loop* curLoop, ValueToValueMapTy& VMap)
        {
            BasicBlock* originalHead = curLoop->getHeader();
            BasicBlock* originalExiting = curLoop->getExitingBlock();
            char* NameSuffix = "dup";
            std::vector<BasicBlock*> dupedBBs;
            for(auto BI = curLoop->getBlocks().begin(); BI!=curLoop->getBlocks().end(); BI++)
            {
                const BasicBlock* BB = *BI;

                // Create a new basic block and copy instructions into it!
                BasicBlock *CBB = CloneBasicBlock(BB, VMap, NameSuffix,originalHead->getParent());

                // Add basic block mapping.
                VMap[BB] = CBB;
                dupedBBs.push_back(CBB);



            }
            // we will add instructions to compute new bounds -- right before the header
            // of the original loop

            Value* newHead = VMap[originalHead];
            // the original exiting block would branch to outsize of the
            // loop, what ever goes out of the loop, we redirect it to the new head
            // FIXME:we make assumption about the loop structure which is only true
            // for our benchmark

            TerminatorInst* exitingIns = originalExiting->getTerminator();
            int numSuc = exitingIns->getNumSuccessors();
            for(int sucInd = 0; sucInd<numSuc; sucInd++)
            {
                BasicBlock* curSuc = exitingIns->getSuccessor(sucInd);
                // the successor is not part of the loop
                if(VMap.find(curSuc)==VMap.end())
                {
                    exitingIns->setSuccessor(sucInd,dyn_cast<BasicBlock>(newHead));
                }
            }
            //errs()<<"Terminator "<<*exitingIns<<" XXXXXXXXXXXXXXXXXXXXXXXX\n";
            // now through the duplicated basic block
            for(auto dupBBIter = dupedBBs.begin();dupBBIter!= dupedBBs.end();dupBBIter++)
            {
                BasicBlock* curBB = *dupBBIter;

                for(auto dupInsIter = curBB->begin(); dupInsIter!= curBB->end(); dupInsIter++)
                {
                    Instruction* curIns = dyn_cast<Instruction>(dupInsIter);
                    PHINode* curPhi = dyn_cast<PHINode>(dupInsIter);
                    if(curPhi)
                    {
                        int numIncomingEdges = curPhi->getNumIncomingValues();
                        for(int i=0; i < numIncomingEdges; i++)
                        {
                            BasicBlock* incomingBlock = curPhi->getIncomingBlock(i);
                            if(VMap.find(incomingBlock)!=VMap.end())
                            {
                                BasicBlock* newIncomingBlock = dyn_cast<BasicBlock>(VMap[incomingBlock]);
                                curPhi->setIncomingBlock(i,newIncomingBlock);
                            }
                            else
                            {
                                curPhi->setIncomingBlock(i,originalExiting);
                            }

                        }
                    }
                    int numOperands = curIns->getNumOperands();
                    for(int opInd = 0; opInd < numOperands; opInd++)
                    {
                        Value* originalOperand = curIns->getOperand(opInd);
                        if(VMap.find(originalOperand)!=VMap.end())
                        {
                            Value* newOperand = VMap[originalOperand];
                            curIns->setOperand(opInd,newOperand);
                        }
                    }
                }
            }
            errs()<<"=========================================end of clone=========\n";



        }
        void printInstructionTree(Instruction* m, std::vector<Instruction*>& storage)
        {
            if(std::find(storage.begin(),storage.end(),m)!=storage.end())
                return;
            storage.push_back(m);
            for(int opInd = 0; opInd < m->getNumOperands(); opInd++)
            {
                Instruction* curOperandIns = dyn_cast<Instruction>(m->getOperand(opInd));
                if(curOperandIns)
                {
                    errs()<<*curOperandIns<<"\n";
                    printInstructionTree(curOperandIns, storage);
                }
                errs()<<"-----------------------\n";
            }
        }



        void dfsBackward(std::vector<Value*>& storage, PHINode*& initPhi, Instruction* curHop, Loop* curLoop=NULL)
        {
            if(std::find(storage.begin(),storage.end(),curHop)!=storage.end())
                return;
            storage.push_back(curHop);
            if(dyn_cast<PHINode>(curHop))
            {
                // if phi is having one incoming edge from outside the loop
                // and the value is from outside the loop
                // then this is the initPhi
                // FIXME:Again we make assumptions about loop structure
                PHINode* curPhi = dyn_cast<PHINode>(curHop);

                if(curLoop && curLoop->getHeader() == curPhi->getParent() )
                    initPhi = curPhi;


            }
            for(int i=0; i<curHop->getNumOperands(); i++)
            {
                Value* curOperand = curHop->getOperand(i);
                Instruction* curIns = dyn_cast<Instruction>(curOperand);

                if(curIns)
                {
                    dfsBackward(storage, initPhi, curIns, curLoop);
                }
            }
        }


        CmpInst* getExitCmp(Loop* curLoop)
        {
            BasicBlock* lastBlock = curLoop->getExitingBlock();
            BranchInst* term = dyn_cast<BranchInst>(lastBlock->getTerminator());
            assert(term->isConditional());
            CmpInst* compareCondition = dyn_cast<CmpInst>(term->getOperand(0));
            return compareCondition;
        }

        BasicBlock* getTakenTarget(BasicBlock* exitingBB)
        {
            BranchInst* term = dyn_cast<BranchInst>(exitingBB->getTerminator());
            // 0th is the condition, 1st is the taken
            Value* taken = term->getOperand(2);
            return dyn_cast<BasicBlock>(taken);
        }


        bool runOnFunction(Function &F) override
        {
            //(*out_c).write_escaped(F.getName()) << '\n';
            DependenceAnalysis* DA = getAnalysisIfAvailable<DependenceAnalysis>();
            /* USE this line to print out the info about memory reference independence */
            if(printDA)
            {
                DA->print(errs(),F.getParent());
                return false;
            }
            LoopInfo* li = getAnalysisIfAvailable<LoopInfo>();
            //ScalarEvolution *se = getAnalysisIfAvailable<ScalarEvolution>();
            ValueToValueMapTy VMap;
            // this part duplicate the outermost loop
            // and change the bound
            for(auto iter = li->begin(); iter!= li->end(); iter++)
            {
                Loop* curLoop = *iter;

                if(curLoop->getLoopDepth()==1)
                {
                    // this is the outer most loop
                    // find the loop bound first
                    cloneLoop(curLoop, VMap);
                    Value* lowerBound;
                    Value* upperBound;
                    CmpInst* exitCmp = getExitCmp(curLoop);

                    BasicBlock* takenSuccessor = getTakenTarget(curLoop->getExitingBlock());

                    bool exitOnTaken = (curLoop->getBlocks().end()==std::find(curLoop->getBlocks().begin(),
                                                 curLoop->getBlocks().end(),
                                                 takenSuccessor));


                    CmpInst::Predicate p = exitCmp->getPredicate();

                    if(p==CmpInst::ICMP_SGT && exitOnTaken)
                    {
                        // when we exit with SGT flag being true
                        // the upper bound is the second operand
                        upperBound = exitCmp->getOperand(1);
                        // we then need to find phiNode which defines the lower bound
                        // we trace back where the other operand comes from
                        // it must contains PHINode and one of the PHINode is taking in
                        // value from outside the loop -- that shall be the lower bound
                        std::vector<Value*> nodeChain;
                        PHINode* initializationPhi=NULL;

                        dfsBackward(nodeChain, initializationPhi,exitCmp, curLoop);
                        if(initializationPhi!=NULL)
                            errs()<<"init phi"<<*initializationPhi<<"\n";
                        else
                            assert(false && "cannot find init phi");
                        int numIncomingBlocks = initializationPhi->getNumIncomingValues();
                        BasicBlock* beforeLoop=NULL;
                        int lowerBoundInd = -1;
                        for(int bInd = 0; bInd < numIncomingBlocks; bInd++)
                        {
                            BasicBlock* pred = initializationPhi->getIncomingBlock(bInd);
                            if(curLoop->getBlocks().end()==std::find(curLoop->getBlocks().begin(),
                                                 curLoop->getBlocks().end(),
                                                 pred))
                            {
                                lowerBound = initializationPhi->getIncomingValueForBlock(pred);
                                lowerBoundInd = bInd;
                                beforeLoop = pred;
                            }
                        }
                        assert(beforeLoop && "cannot find BB before loop for assignment insertion\n");
                        // now both upper and lower bound are found
                        // compute a new value by shifting upperbound to the right by 1
                        // this be the upperbound/lowerbound for the first/second dup
                        // the original upperbound becomes the upper bound for the second dup
                        TerminatorInst* beforeLoopTerm = beforeLoop->getTerminator();
                        IRBuilder<> builder(beforeLoop);
                        Value* halfUpper = builder.CreateLShr(upperBound,1);
                        Constant* const1 = ConstantInt::get(halfUpper->getType(),1);

                        Value* halfUpperP1 = builder.CreateAdd(halfUpper,const1);
                        Instruction* halfUpperIns = dyn_cast<Instruction>(halfUpper);
                        Instruction* halfUpperP1Ins =  dyn_cast<Instruction>(halfUpperP1);

                        halfUpperIns->moveBefore(beforeLoopTerm);
                        halfUpperP1Ins->moveBefore(beforeLoopTerm);
                        // set new upper bound for first copy
                        exitCmp->setOperand(1,halfUpper);
                        // set new lower bound for second copy
                        Value* newInitPhi = VMap[initializationPhi];
                        PHINode* newPhi = dyn_cast<PHINode>(newInitPhi);
                        newPhi->setIncomingValue(lowerBoundInd,halfUpperP1);

                    }
                    else
                    {
                        //FIXME: other scenarios for loop bound determination
                        assert(false && "currently not implemented, go ahead and add the logic");
                    }

                }

            }



            return true;

        }
        virtual void getAnalysisUsage(AnalysisUsage &AU) const override
        {
            AU.addRequired<LoopInfo>();
            AU.addRequired<DependenceAnalysis>();
            AU.addRequired<ScalarEvolution>();
        }
    };
}

char Genpar::ID = 0;
static RegisterPass<Genpar> X("gen-par", "generate par -- generate the parallelizable IR");
FunctionPass *llvm::createGenParPass(llvm::raw_ostream &OS, bool DAInfo)
{
    struct Genpar* gp = new Genpar(OS, DAInfo);
    return gp;
}


