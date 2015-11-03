
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/GenSynthC/GenSynthC.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "GenCFunc.h"
#include "GenCFuncUtil.h"
using namespace llvm;
using namespace GenCFunc;

namespace boost{
    void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        exit(1);
    }
}


namespace {
  // Hello - The first implementation, without getAnalysisUsage.
  struct GenSynthC : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    llvm::raw_ostream* out_c;
    // when generating CPU, all the arbitray int types
    // are made to be either 32 or 64 bit integer
    // also, the channels are such that
    bool generatingCPU;

    GenSynthC() : ModulePass(ID) {}
    GenSynthC(llvm::raw_ostream& OS, bool targetCPU) : ModulePass(ID){
        out_c= &OS;
        generatingCPU = targetCPU;
    }
    // we will iterate through the functions,
    // if they are dppgenerated, we do do normal stuff
    // if they are transformed, we do another stuff
    bool runOnModule(Module &M) override {
        (*out_c) << "// GenSynthC: ";
        setGeneratingCPU(generatingCPU);
        (*out_c).write_escaped(M.getName()) << '\n';
        if(getGeneratingCPU())
            (*out_c) << "#include \"comm.h\"\n ";
        else
            (*out_c) << "#include \"ap_int.h\"\n ";
        std::vector<NormalCFuncGenerator*> normalFuncGenerators;
        std::vector<PipelinedCFuncGenerator*> pipelineFuncGenerators;
        for(auto funcIter = M.begin(); funcIter!=M.end(); funcIter++)
        {

            Function* curFunc = &(*funcIter);
            errs()<<"check out function "<<curFunc->getName()<<"\n";
            if(!curFunc->hasFnAttribute(TRANSFORMEDATTR))
            {
                NormalCFuncGenerator* ncf= new NormalCFuncGenerator(curFunc,*out_c);
                normalFuncGenerators.push_back(ncf);
            }
            else
            {
                PipelinedCFuncGenerator* pcf = new PipelinedCFuncGenerator(curFunc,*out_c);
                pipelineFuncGenerators.push_back(pcf);
            }
        }
        for(auto ncgenIter = normalFuncGenerators.begin(); ncgenIter!=normalFuncGenerators.end();
            ncgenIter++)
        {
            (*ncgenIter)->generateFunction();
        }
        for(auto pcgenIter = pipelineFuncGenerators.begin(); pcgenIter!=pipelineFuncGenerators.end();
            pcgenIter++)
        {
            (*pcgenIter)->generateFunction();
        }
        return false;
    }
  };
}

char GenSynthC::ID = 0;
static RegisterPass<GenSynthC> X("gensynthc", "gensynthc Pass");
ModulePass *llvm::createGenSynthCPass(llvm::raw_ostream &OS, bool targetCPU)
{
    struct GenSynthC* generateCPath = new GenSynthC(OS,targetCPU);
    return generateCPath;
}



