
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/GenSynthC/GenSynthC.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "GenCFunc.h"
using namespace llvm;
using namespace GenCFunc;
namespace {
  // Hello - The first implementation, without getAnalysisUsage.
  struct GenSynthC : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    llvm::raw_ostream* out_c;

    GenSynthC() : ModulePass(ID) {}
    GenSynthC(llvm::raw_ostream& OS) : ModulePass(ID){
        out_c= &OS;
    }
    // we will iterate through the functions,
    // if they are dppgenerated, we do do normal stuff
    // if they are transformed, we do another stuff
    bool runOnModule(Module &M) override {
        (*out_c) << "// GenSynthC: ";
        (*out_c).write_escaped(M.getName()) << '\n';
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
ModulePass *llvm::createGenSynthCPass(llvm::raw_ostream &OS)
{
    struct GenSynthC* generateCPath = new GenSynthC(OS);
    return generateCPath;
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
