
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "llvm/Transforms/DecoupleMemAccess/DecoupleMemAccess.h"
#include "llvm/Transforms/BoostException.h"
#include "llvm/IR/Instructions.h"
using namespace llvm;


/*namespace boost{
    void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        exit(1);
    }
}*/

namespace {
    struct DecoupleMemAccess : public ModulePass {
        static char ID;
        bool burstAccess;
        DecoupleMemAccess(bool burst=true) : ModulePass(ID) {
            burstAccess=burst;
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
            std::vector<Function*> memoryAccessFunctions;
            // top level functions layout pipeline accessed at the end
            std::vector<Function*> pipelineLevelFunctions;
            for(auto funcIter = M.begin(); funcIter!=M.end(); funcIter++)
            {
                Function& curFunc = *funcIter;
                // iterate through the basicblocks and see if the loaded value
                // is written to a channel out put
                std::map<Instruction*, Argument*> load2Port;
                std::map<Instruction*, Argument*> store2Port;
                for(auto bbIter = curFunc.begin(); bbIter!= curFunc.end(); bbIter++)
                {
                    BasicBlock& curBB = *bbIter;
                    for(auto insIter = curBB.begin(); insIter!=curBB.end(); insIter++)
                    {
                        Instruction& curIns = *insIter;
                        if(isa<LoadInst>(curIns))
                        {

                        }
                        else if(isa<StoreInst>(curIns))
                        {

                        }
                    }
                }
            }



        }
        //virtual void getAnalysisUsage(AnalysisUsage &AU) const override;
    };
}


char DecoupleMemAccess::ID = 0;
static RegisterPass<DecoupleMemAccess> X("decouple-mem-access", "decouple memory access Pass");
ModulePass *llvm::createDecoupleMemAccessPass(bool burst)
{
    struct DecoupleMemAccess* decoupleMemory = new DecoupleMemAccess(burst);
    return decoupleMemory;
}


