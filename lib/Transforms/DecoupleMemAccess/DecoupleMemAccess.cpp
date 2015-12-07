
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "llvm/Transforms/DecoupleMemAccess/DecoupleMemAccess.h"
#include "llvm/Transforms/BoostException.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
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


    struct DecoupleMemAccess : public ModulePass {
        static char ID;
        bool burstAccess;
        std::map<Type*,  StructType*> elementType2BurstInfo;
        DecoupleMemAccess(bool burst=true) : ModulePass(ID) {
            burstAccess=burst;
        }

        bool analyzeLoadBurstable(LoadInst* li,LoopInfo* funcLI)
        {
            return false;
        }

        bool runOnModule(Module &M) override
        {
            // LOAD: look at each generated function, each a load is followed by writing
            // to a pointer argument with attribute denoting it to be write channel "CHANNELWR"
            // we change the load:
            // 0. replace the original CHANNELWR channel with an address port and a size port (optional)
            // 1. in the absence of burst, replace the load instruction with an address write to
            // the address port
            // 2. in the presence of burst, move load outside of the involved loop and make one
            // address write + one size write
            // 3. add in new function to read memory and write to fifo....the same fifo the downstream
            // guys are reading -- this newly added function would break them into reasonable bursts
            // STORE: let's not do this first
            // 0. replace the original store with an address port and a size port(optional) and a data port
            // 1. in the case of burst, address req get moved outside but actual data is written into the
            // data port as they get created
            // newly created memory access function
            errs()<<"into func run\n";
            std::vector<Function*> memoryAccessFunctions;
            // top level functions layout pipeline accessed at the end
            std::vector<Function*> pipelineLevelFunctions;
            std::vector<Function*> topLevelFunctions;
            for(auto funcIter = M.begin(); funcIter!=M.end(); funcIter++)
            {
                Function& curFunc = *funcIter;
                if(!curFunc.hasFnAttribute(GENERATEDATTR))
                {
                    if(curFunc.hasFnAttribute(TRANSFORMEDATTR))
                        topLevelFunctions.push_back(funcIter);
                    continue;
                }
                LoopInfo* funcLI=&getAnalysis<LoopInfo>(curFunc);
                // iterate through the basicblocks and see if the loaded value
                // is written to a channel out put -- we do not convert stores
                // the argument involved here are all old arguments
                std::map<Instruction*, Argument*> load2Port;
                std::set<Argument*> addressArg;
                std::set<Argument*> burstedArg;
                std::map<Instruction*, Argument*> store2Port;
                for(auto bbIter = curFunc.begin(); bbIter!= curFunc.end(); bbIter++)
                {
                    BasicBlock& curBB = *bbIter;
                    for(auto insIter = curBB.begin(); insIter!=curBB.end(); insIter++)
                    {
                        Instruction& curIns = *insIter;
                        if(isa<LoadInst>(curIns))
                        {
                            LoadInst& li = cast<LoadInst>(curIns);
                            // we check if the result of this is directly written to an output port
                            // using store
                            int numUser = std::distance(curIns.user_begin(),curIns.user_end());
                            if(numUser==1 )
                            {
                                auto soleUserIter = curIns.user_begin();
                                if(isa<StoreInst>(*soleUserIter))
                                {
                                    StoreInst* si = cast<StoreInst>(*soleUserIter);
                                    Value* written2 = si->getPointerOperand();
                                    if(isa<Argument>(*written2))
                                    {
                                        Argument& channelArg = cast<Argument>(*written2);
                                        // make sure this is wrchannel
                                        if(isArgChannel(&channelArg))
                                        {
                                            load2Port[&li] = &channelArg;
                                            addressArg.insert(&channelArg);
                                            if(burstAccess&& analyzeLoadBurstable(&li,funcLI))
                                                burstedArg.insert(&channelArg);

                                        }
                                    }
                                }
                            }
                        }
                        //FIXME: not doing storeInst
                        else if(isa<StoreInst>(curIns))
                        {

                        }
                    }
                }
                // now we have the loadInst which will be converted to pipelined mem access
                // we need to create a bunch of new functions -- we then use these new functions
                // in our new top levels --- after which everything original is deleted
                std::string functionName = curFunc.getName();
                functionName += "MemTrans";
                Type* rtType = curFunc.getReturnType();
                // old to new argument map
                std::map<Argument*,Argument*> oldDataFifoArg2newAddrArg;
                // these are arguments of the newly created function
                std::map<Argument*,Argument*> addressArg2SizeArg;
                std::vector<Type*> paramsType;

                for(auto argIter = curFunc.arg_begin();argIter!=curFunc.arg_end();argIter++)
                {
                    Argument* curArg = &cast<Argument>(*argIter);
                    paramsType.push_back(curArg->getType());
                }
                for(int numBurstSize = 0; numBurstSize<burstedArg.size();numBurstSize++)
                {
                    paramsType.push_back(PointerType::get(Type::getInt32Ty(M.getContext()),0));
                }
                FunctionType* newFuncType = FunctionType::get(rtType,ArrayRef<Type*>(paramsType),false);
                Constant* newFunc = M.getOrInsertFunction(functionName, newFuncType  );
                Function* memTransFunc = cast<Function>(newFunc);

                auto  newArgIter = memTransFunc->arg_begin();
                for(auto oldArgIter = curFunc.arg_begin();
                    oldArgIter!=curFunc.arg_end();
                    oldArgIter++, newArgIter++)
                {
                    Argument* oldArg = &cast<Argument>(*oldArgIter);
                    Argument* newArg = &cast<Argument>(*newArgIter);
                    oldDataFifoArg2newAddrArg[oldArg] = newArg;
                }
                auto burstedArgIter = burstedArg.begin();
                while(newArgIter!=memTransFunc->arg_end())
                {
                    Argument* newBurstArg = &cast<Argument>(*newArgIter);
                    Argument* originalDataFifoArg = *burstedArgIter;
                    Argument* newAddressArg = oldDataFifoArg2newAddrArg[originalDataFifoArg];
                    addressArg2SizeArg[newAddressArg] = newBurstArg;
                    newArgIter++;
                    burstedArgIter++;
                }

                // if bursted access is in a loop, we want to take it out of the loop
                // make it pre-header -- to do this, we associate each loadIns with
                std::map<BasicBlock*,std::vector<Instruction*>*> bb2BurstedLoads;
                for(auto load2PortIter = load2Port.begin(); load2PortIter!=load2Port.end(); load2PortIter++)
                {
                    Instruction* ldInst = load2PortIter->first;
                    Argument* ldArg = load2PortIter->second;
                    BasicBlock* ldParent = ldInst->getParent();
                    if(burstedArg.count(ldArg))
                    {
                        // this load is to be bursted
                        if(!bb2BurstedLoads.count(ldParent))
                            bb2BurstedLoads[ldParent] = new std::vector<Instruction*>();
                        bb2BurstedLoads[ldParent]->push_back(ldInst);
                    }
                }
                // now memTransFunc is the new function
                // we will now populate it, we mirror everybb
                std::map<BasicBlock*,BasicBlock*> oldBB2NewBB;
                // also we need a few preheaders to do the burst load
                for(auto bbIter = curFunc.begin(); bbIter!= curFunc.end(); bbIter++)
                {
                    BasicBlock& oldBB = *bbIter;


                }

                // FIXME: release bb2BurstedLoads

            }

            return false;

        }
        virtual void getAnalysisUsage(AnalysisUsage &AU) const override
        {
            AU.addRequired<LoopInfo>();
        }
    };
}

char DecoupleMemAccess::ID = 0;
static RegisterPass<DecoupleMemAccess> X("decouple-mem-access", "decouple memory access Pass");
ModulePass *llvm::createDecoupleMemAccessPass(bool burst)
{


    struct DecoupleMemAccess* decoupleMemory = new DecoupleMemAccess(burst);

    return decoupleMemory;
}


