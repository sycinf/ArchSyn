
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/GenPar/Genpar.h"
#include "llvm/Transforms/BoostException.h"

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
        Genpar() : FunctionPass(ID) {}
        Genpar(llvm::raw_ostream& OS) : FunctionPass(ID){
            out_c= &OS;
        }
        /*
         * we do two things here -- determine the dependence direction
         * also dump out the dep checking code
         *
         */
        bool runOnFunction(Function &F) override
        {
            (*out_c) << "// Genpar: ";
            (*out_c).write_escaped(F.getName()) << '\n';

            for(auto bbIter = F.begin(); bbIter!=F.end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                curBB->setName("BB");
                errs()<<"bb: " <<curBB->getName()<<"\n";

            }


            std::vector<Instruction*> allIns;
            for(auto bbIter = F.begin(); bbIter!=F.end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                errs()<<"bb: " <<curBB->getName()<<"\n";
                for(auto insIter = curBB->begin(); insIter!=curBB->end(); insIter++)
                {
                    errs()<<"\n"<<*insIter<<"\n";
                    allIns.push_back(insIter);
                }
            }


            DependenceAnalysis* DA = getAnalysisIfAvailable<DependenceAnalysis>();

            DA->print(errs(),F.getParent());

            return false;

        }
        virtual void getAnalysisUsage(AnalysisUsage &AU) const override
        {
            AU.addRequired<LoopInfo>();
            AU.addRequired<DependenceAnalysis>();
        }
    };
}

char Genpar::ID = 0;
static RegisterPass<Genpar> X("gen-par", "generate par -- generate the code and the checking code");
FunctionPass *llvm::createGenParPass(llvm::raw_ostream &OS)
{
    struct Genpar* gp = new Genpar(OS);
    return gp;
}


