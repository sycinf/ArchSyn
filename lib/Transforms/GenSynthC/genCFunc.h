#ifndef GENCFUNC_H
#define GENCFUNC_H
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "GenCFuncUtil.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include "GenCInsn.h"
#include <boost/lexical_cast.hpp>
#include <iterator>
using namespace llvm;
using namespace std;
namespace GenCFunc {
    class FuncGenerator{
    protected:
        Function* func;
        raw_ostream& out_cfile;
        std::set<Argument*> channelArg;
        std::set<Instruction*> specialExclude;
    public:
        FuncGenerator(Function* f, raw_ostream& os):out_cfile(os)
        {
            func = f;
        }
        void generateFunctionDecl()
        {
            // here we just print out the function name and return type,
            // the argument would depend on the actual attribute
            Type* returnType = func->getReturnType();
            // we do not use the functionType string generator, we check
            // the argument individually -- coz we need to differentiate
            // the argument for channels v.s. normal argument
            std::string signatureLine = getLLVMTypeStr(returnType);
            signatureLine += " ";
            signatureLine += func->getName();
            signatureLine +="(";
            printTabbedLines(out_cfile,signatureLine);
            addBarSubTabs(true);
            for(auto argIter = func->arg_begin(); argIter!=func->arg_end(); /*argIter++*/)
            {
                Argument* curFuncArg = &(cast<Argument>(*argIter));
                Type* type2Print;
                bool argIsChannel = isArgChannel(curFuncArg);
                if(argIsChannel)
                {
                    assert(curFuncArg->getType()->getTypeID() == Type::PointerTyID && "channel is not a pointer type");
                    PointerType* curArgType = &(cast<PointerType>(*(curFuncArg->getType())));
                    type2Print = curArgType->getPointerElementType();

                }
                else
                    type2Print = curFuncArg->getType();
                std::string curArgTypeStr = getLLVMTypeStr(type2Print);

                if(argIsChannel && getGeneratingCPU())
                    curArgTypeStr = "channel<"+curArgTypeStr+">&";
                else if(argIsChannel)
                    curArgTypeStr = curArgTypeStr+"* ";

                std::string varTypewName = curArgTypeStr+" ";
                varTypewName += curFuncArg->getName();
                if(++argIter != func->arg_end())
                    varTypewName +=",";
                printTabbedLines(out_cfile,varTypewName);
            }
            addBarSubTabs(false);
            printTabbedLines(out_cfile,")");
        }
        void generateFunctionBody()
        {
            // first give each BB a name
            std::set<std::string> usedBbNames;
            int bbCount = 0;
            for(auto bbIter = func->begin(); bbIter!=func->end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));

                if(curBB->getName().size()==0)
                {
                    std::string bbPrefix("BB_assignedName_");
                    std::string bbIndStr = boost::lexical_cast<std::string>(bbCount);
                    std::string bbNameBase = bbPrefix+bbIndStr;
                    std::string newBbName = bbNameBase;
                    int vCountSuf = 0;
                    while(usedBbNames.count(newBbName))
                    {
                        newBbName = bbNameBase+"v"+boost::lexical_cast<std::string>(vCountSuf);
                        vCountSuf++;
                    }
                    curBB->setName(newBbName);
                    usedBbNames.insert(newBbName);
                    bbCount+=1;
                }
                else
                {
                    std::string legal = curBB->getName();
                    std::replace(legal.begin(),legal.end(),'.','_');
                    curBB->setName(legal);
                    usedBbNames.insert(legal);
                }
            }


            // vector of variable declaration
            std::vector<std::string> varDecl;
            std::map<BasicBlock*, std::vector<std::string>*> bbContentStr;
            std::map<BasicBlock*, std::vector<std::string>*> phiPreAssign;
            // go through once to generate the basic var and content
            for(auto bbIter = func->begin(); bbIter!=func->end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                std::vector<std::string>* curBBContent = new std::vector<std::string>();
                bbContentStr[curBB] = curBBContent;

                for(auto insIter = curBB->begin(); insIter!=curBB->end(); insIter++)
                {
                    Instruction* curIns = &(cast<Instruction>(*insIter));
                    if(specialExclude.count(curIns))
                        continue;
                    InstructionGenerator ig(curIns,&varDecl,curBBContent,&phiPreAssign);
                    ig.generateInstruction();
                }
            }
            // now all the declaration and strings are generated properly
            printTabbedLines(out_cfile,"{");
            addBarSubTabs(true);
            for(auto varDeclIter = varDecl.begin(); varDeclIter!=varDecl.end(); varDeclIter++)
            {
                std::string curDecl = *varDeclIter;
                printTabbedLines(out_cfile,curDecl);
            }
            for(auto bbIter = func->begin(); bbIter!=func->end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                std::string bbHeadStr = curBB->getName();
                printTabbedLines(out_cfile,bbHeadStr+":");
                addBarSubTabs(true);

                std::vector<std::string>* curBBContent = bbContentStr[curBB];
                int numInstLeft = curBBContent->size();
                for(auto insStrIter = curBBContent->begin(); insStrIter!=curBBContent->end(); insStrIter++)
                {
                    if(numInstLeft==1 && phiPreAssign.count(curBB))
                    {
                        std::vector<std::string>* curBBPrePhiStr = phiPreAssign[curBB];
                        for(auto prePhiStrIter = curBBPrePhiStr->begin();
                            prePhiStrIter!=curBBPrePhiStr->end();
                            prePhiStrIter++)
                        {
                            printTabbedLines(out_cfile,*prePhiStrIter);
                        }
                        delete curBBPrePhiStr;
                    }
                    std::string curInsStr = *insStrIter;
                    printTabbedLines(out_cfile,curInsStr);
                    numInstLeft--;
                }
                delete curBBContent;
                addBarSubTabs(false);
            }

            addBarSubTabs(false);
            printTabbedLines(out_cfile,"}");



        }





    };
    class PipelinedCFuncGenerator:FuncGenerator{
    public:
        PipelinedCFuncGenerator(Function* f, raw_ostream& os):FuncGenerator(f,os){}
        void generateChannelAllocation()
        {
            // iterate through the alloca in the entry block, and make them exclude
            BasicBlock& myOnlyBlock = func->getEntryBlock();
            for(auto insIter = myOnlyBlock.begin(); insIter!=myOnlyBlock.end(); insIter++)
            {
                if(isa<AllocaInst>(*insIter))
                {
                    // declare the channel object
                    specialExclude.insert(insIter);
                }
                else if (isa<CallInst>(*insIter))
                {
                    // create special pthread library to make things happen
                    specialExclude.insert(insIter);
                }
            }
        }

        void generateFunction()
        {
            FuncGenerator::generateFunctionDecl();
            generateChannelAllocation();
            FuncGenerator::generateFunctionBody();

        }
    };
    class NormalCFuncGenerator:FuncGenerator{
    public:
        NormalCFuncGenerator(Function* f, raw_ostream& os):FuncGenerator(f,os){}
        void generateFunction()
        {
            FuncGenerator::generateFunctionDecl();
            FuncGenerator::generateFunctionBody();
        }
    };

}


#endif // GENCFUNC_H
