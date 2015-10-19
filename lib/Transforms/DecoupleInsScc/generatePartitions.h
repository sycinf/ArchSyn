#ifndef GENERATEPARTITIONS_H
#define GENERATEPARTITIONS_H

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

#define LONGLATTHRESHOLD 5
#define DIS_MSG errs()<<"Decoupling Pass Message: "


namespace partGen{
    struct DAGNode;
    struct DAGPartition;
    typedef std::map<DAGNode*, std::vector<DAGPartition*>*> DagNode2PartitionMap;
    typedef std::map<BasicBlock*, std::set<Instruction*>*> BBMap2Ins;
    typedef BBMap2Ins::iterator BBMapIter;

    struct DecoupleInsScc : public FunctionPass {
        static char ID; // Pass identification, replacement for typeid
        Function* targetFunc;
        //BasicBlock* newSingleBB;
        IRBuilder<>* newReplacementBBBuilder;
        bool controlFlowDuplication;
        std::vector<DAGNode*> collectedDagNode;
        std::vector<DAGPartition*> collectedPartition;
        std::map<const Instruction *, DAGNode *> dagNodeMap;
        DagNode2PartitionMap dagPartitionMap;
        DecoupleInsScc() : FunctionPass(ID) {
        }
        void setControlFlowDuplication(bool cfDup);
        bool generatePartition();
        bool runOnFunction(Function &F) override;
        void checkAcyclicDependency();
        bool DFSFindPartitionCycle(DAGPartition* dp);
        virtual void getAnalysisUsage(AnalysisUsage &AU) const override;
        std::vector<DAGPartition*>* getPartitionFromIns(Instruction* ins);
    };

    struct DAGNode
    {
        std::vector<InstructionGraphNode*> dagNodeContent;
        bool singleIns;
        int sccLat;
        bool hasMemory;
        bool expensive;
        bool covered;
        int seqNum;
        ~DAGNode();
        void init();
        void print();
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
        // we also need another mapping, so phi can know which
        // bb in the new function to use for each incoming edge
        std::map<BasicBlock*,BasicBlock*> partitionPhiRemap;

        std::set<BasicBlock*> singleSucBBs;
        // the dominator for all basicblocks in this partition
        // the entry block for this partition will be here
        BasicBlock* dominator;
        // BBs containing the source of the instructions assigned
        // to this partition
        BBMap2Ins sourceBBs;
        // BBs containing the actual instruction assigned to this
        // this partition
        BBMap2Ins insBBs;
        // all relevant BBs, including insBB and sourceBB and
        // all the necessary BBs for control flow purpose
        std::set<BasicBlock*> AllBBs;

        void init(struct DecoupleInsScc* tt );
        void print();
        void addDagNode(DAGNode* dagNode,DagNode2PartitionMap &nodeToPartitionMap );
        void generateBBList();
        void trimBBList();
        // whole bunch of helper functions
        void checkDominator();
        bool allExitOutsideLoop();
        bool isFlowOnlyBB(BasicBlock* curBB);
        bool terminatorNotLocal(BasicBlock* curBB);
        bool needBranchTag(BasicBlock* curBB);
        bool hasActualInstruction(Instruction* target);
        bool receiverPartitionsExist(Instruction* insPt);
        void setupBBStructure();
        Function* generateDecoupledFunction(int seqNum,
                                            std::map<Instruction*,Value*>& ins2Channel,
                                            std::vector<Value*>* argList);


    };
    struct PartitionStrategies
    {
        DecoupleInsScc* top;
        void init(DecoupleInsScc* container)
        {
            top = container;
        }

        bool needANewPartition(DAGPartition* curPartition, DAGNode* curNode)
        {
            // if the last node we added accesses memory or it was a long latency operation
            if((curNode->hasMemory  &&  (curPartition->containLongLatCyc || curPartition->containMemory))||
               ((!curNode->singleIns  && curNode->sccLat >= LONGLATTHRESHOLD)&& (curPartition->containMemory)))
                return true;
            else
                return false;
        }

        bool barrierPartition(std::vector<DAGPartition*>& partitions)
        {
            DIS_MSG<<"Partition Strategy: Simple cut at memory accees node\n";
            std::vector<DAGNode*>& dag = top->collectedDagNode;

            DAGPartition* curPartition = new DAGPartition;
            curPartition->init(top);
            partitions.push_back(curPartition);

            for(unsigned int dagInd = 0; dagInd < dag.size(); dagInd++)
            {
                DAGNode* curDagNode = dag.at(dagInd);
                curPartition->addDagNode(curDagNode ,top->dagPartitionMap);
                // if this is not the last node and we need a new partition
                if(dagInd!=dag.size()-1 && needANewPartition(curPartition,curDagNode))
                {
                    curPartition = new DAGPartition;
                    curPartition->init(top);
                    partitions.push_back(curPartition);
                }

            }
            if(partitions.size()>1)
                return true;
            else
                return false;
        }

    };

}


#endif // GENERATEPARTITIONS_H
