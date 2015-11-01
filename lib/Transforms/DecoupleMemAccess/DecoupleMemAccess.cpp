
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "llvm/Transforms/DecoupleMemAccess/DecoupleMemAccess.h"
using namespace llvm;


namespace boost{
    void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        exit(1);
    }
}

namespace {
    struct DecoupleMemAccess : public ModulePass {
        static char ID;
        bool burstAccess;
        DecoupleMemAccess(bool burst=true) : ModulePass(ID) {
            burstAccess=burst;
        }
        bool runOnModule(Module &M) override
        {
            // look at each generated function, each a load is followed by writing
            // to a pointer
            // argument with attribute denoting it to be write channel "CHANNELWR"
            // we change the load:
            // 0. replace the original CHANNELWR channel with an address chanel
            // 1. to a address write to a address channel (which is newly added)
            //
            // 2. create a function where the the address is the only input
            //    and memory value is the only output
            //
            // 3. add the function to the pendingAdd list
            //
            // 4, in the case of burst, instead of the address channel, we have a channel
            //    of struct cncompassing address and size
            //    the original loop where the address is generated is examined to see
            //    if the address generation can be collapsed out of the loop
            //    this means if the loop is a simple adding counter to some base
            //    address, then the entire loop can be replaced with generation
            //    of base address and size, on the other hand, if there are other stuff
            //    going on in the loop, we try to see if the address generation can be
            //    separated -- meaning have it one level up before the rest of the loop
            //
            // 5, to facilitate test using thread model, the newly generated function
            //    will keep looping til address=0 is received
            //
            // 5.5 add all the newly created function to the module
            // 6, leave the top level function (teh transformed function) to the end
            //    allocate a few more things for address transport, and call the newly
            //    generated function
            //=========================================================================
            // newly created memory access function
            std::vector<Function*> memoryAccessFunctions;
            // top level functions layout pipeline accessed at the end
            std::vector<Function*> pipelineLevelFunctions;
            for(auto funcIter = M.begin(); funcIter!=M.end(); funcIter++)
            {
                Function* curFunc = &(*funcIter);
                // iterate through the basicblocks and see if the loaded value
                // is written to a channel out put
                std::vector<Instruction*> allStreamingMemoryOps;
                for(auto bbIter = curFunc->begin(); bbIter!= curFunc->end(); bbIter++)
                {

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


