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
    // two classes for CPU specific implementation
    // things -- how to pack argument
    class CPUChannelGenerator{
    private:
        AllocaInst* insn;




    public:
        CPUChannelGenerator(AllocaInst* _insn)
        {
            insn = _insn;
        }

        string generateChannelStr()
        {
            std::string fifoName = generateFifoChannelName(insn);
            int numberOfFifo = insn->getNumUses()-1;
            PointerType& ptrType = cast<PointerType>(*insn->getType());
            Type* pointedType = ptrType.getPointerElementType();
            std::string ptedTypeName = getLLVMTypeStr(pointedType);
            std::string channelFifoType = "fifo_channel<"+ptedTypeName+">";
            std::string channelFifoDecl = channelFifoType+" "+fifoName+";";
            // now we have the fifo declared properly, we will need to
            // init it with the
            std::string channelFifoInit = fifoName+".init(";
            channelFifoInit+=boost::lexical_cast<std::string>(numberOfFifo) +");";

            // now is to initialize a channel_info for everybody
            int readSeq = 0;
            std::string allChannelInfoStr = "";
            std::string channelInfoType = "channel_info<"+ptedTypeName+">";
            for(auto user_iter = insn->user_begin(); user_iter != insn->user_end(); user_iter++)
            {
                std::string curChannelInfoName = generateFifoChannelInfoName(insn,*user_iter);
                std::string curChannelInfoDec = channelInfoType+" "+curChannelInfoName+";";

                User* curUser = *user_iter;
                assert(isa<CallInst>(*curUser) && "channel not used by callInst");
                CallInst* funcCallUser = &cast<CallInst>(*curUser);
                int argSeq = getOperandArgSeq(funcCallUser,insn);
                assert(argSeq!=-1 && "could not find argSeq");
                Function* calledFunc = funcCallUser->getCalledFunction();
                bool isWrChannel = cmpChannelAttr(calledFunc->getAttributes(),argSeq,CHANNELWR);
                bool isRdChannel = cmpChannelAttr(calledFunc->getAttributes(),argSeq,CHANNELRD);
                assert((isWrChannel^isRdChannel) && "got to be wr channel or rd channel");

                std::string curChannelInfoInit = curChannelInfoName+".init(";
                curChannelInfoInit+="&"+fifoName+",";
                if(isWrChannel)
                    curChannelInfoInit+="-1";
                else
                {
                    curChannelInfoInit+=boost::lexical_cast<std::string>(readSeq);
                    readSeq++;
                }
                curChannelInfoInit+=");";
                allChannelInfoStr+=curChannelInfoDec+"\n";
                allChannelInfoStr+=curChannelInfoInit+"\n";

            }
            assert(readSeq==numberOfFifo && "fifo number mismatch");
            std::string rtStr = channelFifoDecl+"\n";
            rtStr+= channelFifoInit+"\n";
            rtStr+=allChannelInfoStr;
            return rtStr;
        }

    };
    class CPUThreadInputGenerator{
        // this is to figure out the channel
        // name and to have the channel info instantiated
        CallInst* cinsn;
    public:
        CPUThreadInputGenerator(CallInst* _cinsn)
        {
            cinsn = _cinsn;
        }

        string generateInputPackage()
        {
            Function* calledFunc = cinsn->getCalledFunction();
            int packageSeq = getFuncSeq(calledFunc);
            assert(packageSeq!=-1 &&"cannot find function in module");
            auto calleeArgIter = calledFunc->arg_begin();

            std::string returnPackageType = generateInputPackStructType(packageSeq);
            std::string returnPackageName = "argPackage"+boost::lexical_cast<std::string>(packageSeq);

            std::string returnPackageStr = returnPackageType+" "+returnPackageName+"={";

            for(unsigned argSeq = 0 ;
                argSeq < cinsn->getNumArgOperands();
                argSeq++,calleeArgIter++)
            {
                Value* curArgInCallInst = cinsn->getArgOperand(argSeq);
                Argument* curFuncArg = &(cast<Argument>(*calleeArgIter));
                bool argIsChannel = isArgChannel(curFuncArg);
                std::string curPackEntry = "";
                if(argIsChannel)
                {
                    AllocaInst* channelAllocaInst = &cast<AllocaInst>(*curArgInCallInst);
                    std::string channelInfoName = generateFifoChannelInfoName(channelAllocaInst,cinsn);
                    curPackEntry+="&";
                    curPackEntry+=channelInfoName;
                }
                else
                {

                    if(isa<Argument>(*curArgInCallInst))
                    {
                        Argument& topLevelArg = cast<Argument>(*curArgInCallInst);
                        curPackEntry+= topLevelArg.getName();
                    }
                    else if(isa<Instruction>(*curArgInCallInst))
                    {
                        Instruction* topLevelIns = &(cast<Instruction>(*curArgInCallInst));
                        curPackEntry+= generateVariableName(topLevelIns);
                    }
                    else
                        llvm_unreachable("non argument nore instruction taken in by generated function");

                }
                returnPackageStr+= curPackEntry;
                if(argSeq!=cinsn->getNumArgOperands()-1)
                    returnPackageStr+=",";
            }
            if(calledFunc->getReturnType()->getTypeID()!=Type::VoidTyID)
            {
                std::string returnTypeStr = getLLVMTypeStr(calledFunc->getReturnType());
                std::string returnedVarName = generateVariableName(cinsn);
                std::string declareReturnElement = returnTypeStr+" "+returnedVarName+";\n";
                returnPackageStr = declareReturnElement+returnPackageStr;
                if( cinsn->getNumArgOperands() > 0)
                    returnPackageStr+=",";
                returnPackageStr+= "&"+returnedVarName;

            }

            returnPackageStr+="};\n";
            return returnPackageStr;
        }



    };


    class FuncGenerator{
    protected:
        Function* func;
        raw_ostream& out_cfile;
        std::set<Argument*> channelArg;
        std::map<Instruction*,std::string> specialExclude;
        std::string bodyPrefixStr;
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
                    curArgTypeStr = "channel_info<"+curArgTypeStr+">*";
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
                    {
                        std::string specialStr= specialExclude[curIns];
                        curBBContent->push_back(specialStr);
                        continue;
                    }
                    InstructionGenerator ig(curIns,&varDecl,curBBContent,&phiPreAssign);
                    ig.generateInstruction();
                }
            }
            // now all the declaration and strings are generated properly
            printTabbedLines(out_cfile,"{");
            addBarSubTabs(true);
            printTabbedLines(out_cfile,bodyPrefixStr);
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
            int numFuncCall = 0;
            Instruction* lastCallInst=0;
            for(auto insIter = myOnlyBlock.begin(); insIter!=myOnlyBlock.end(); insIter++)
            {
                if(getGeneratingCPU())
                {
                    if(isa<AllocaInst>(*insIter))
                    {
                        // declare the channel object

                        AllocaInst* ai = &(cast<AllocaInst>(*insIter));
                        CPUChannelGenerator ccg(ai);
                        string channelStr=ccg.generateChannelStr();
                        FuncGenerator::bodyPrefixStr+=channelStr+"\n";
                        specialExclude[insIter]="";
                    }
                    else if (isa<CallInst>(*insIter))
                    {
                        // create special pthread library to make things happen
                        CallInst* curCallInst = &cast<CallInst>(*insIter);
                        CPUThreadInputGenerator curInputPack(curCallInst);
                        string inputPack = curInputPack.generateInputPackage();
                        FuncGenerator::bodyPrefixStr+= inputPack;
                        // call to the wrapper
                        int packageSeq = getFuncSeq(curCallInst->getCalledFunction());
                        std::string returnPackageName = "argPackage"+boost::lexical_cast<std::string>(packageSeq);


                        std::string pthreadCallWrapper = "pthread_create(&threads[";
                        pthreadCallWrapper+=boost::lexical_cast<std::string>(numFuncCall)+"], &attr, ";
                        pthreadCallWrapper+= curCallInst->getCalledFunction()->getName();
                        pthreadCallWrapper+="wrapper,&"+returnPackageName+");\n";

                        specialExclude[insIter] = pthreadCallWrapper ;
                        lastCallInst=insIter;
                        // the last function call also takes charge of doing the threadjoin
                        numFuncCall++;

                    }
                }
            }
            if(getGeneratingCPU())
            {
                std::string pthreadStuff = "pthread_t threads[";
                pthreadStuff+=boost::lexical_cast<std::string>(numFuncCall);
                pthreadStuff+="];\n";
                pthreadStuff+="pthread_attr_t attr;\n";
                pthreadStuff+="pthread_attr_init(&attr);\n";
                pthreadStuff+="pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);\n";
                FuncGenerator::bodyPrefixStr+=pthreadStuff;
                if(lastCallInst)
                {
                    std::string originalLastFuncCall = specialExclude[lastCallInst];
                    std::string joinStuff = "for (int i=0; i<";
                    joinStuff+= boost::lexical_cast<std::string>(numFuncCall)+"; i++) pthread_join(threads[i], NULL);\n";
                    specialExclude[lastCallInst] = originalLastFuncCall+joinStuff;
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

        std::string generateInputStructType()
        {
            int funcSeqNum = getFuncSeq(this->func);
            return generateInputPackStructType(funcSeqNum);
        }

    public:
        NormalCFuncGenerator(Function* f, raw_ostream& os):FuncGenerator(f,os){}

        std::string getPackageArgType(Argument* curArg)
        {
            Type* curArgType = curArg->getType();
            std::string typeStr = getLLVMTypeStr(curArgType);
            if(isArgChannel(curArg))
            {
                PointerType* curPtTy = &cast<PointerType>(*curArgType);
                Type* channelType = curPtTy->getPointerElementType();
                typeStr=getLLVMTypeStr(channelType);
                typeStr = "channel_info<"+typeStr+">*";
            }
            return typeStr;

        }
        std::string generateReturnValName()
        {
            std::string nameStr = func->getName();
            nameStr+="return_val";
            return nameStr;

        }

        void generateCPUThreadWrapper()
        {
            std::string funcDec = func->getName();
            funcDec = "void* "+funcDec+"wrapper";
            funcDec+="(void* arg)";
            printTabbedLines(out_cfile,funcDec);
            printTabbedLines(out_cfile,"{");
            addBarSubTabs(true);
            // actually calling function with packaged input
            std::string structType = generateInputStructType();
            std::string localPackName = "localPackArg";
            std::string structDec = structType+"* "+localPackName+" = ("+structType+"*)arg;";
            printTabbedLines(out_cfile,structDec);
            // one by one create the argument to pass into the function
            for(auto argIter = func->arg_begin(); argIter != func->arg_end(); argIter++)
            {
                Argument* curArg = &cast<Argument>(*argIter);
                std::string typeStr = getPackageArgType(curArg);
                std::string curArgName = curArg->getName();
                std::string assignmentStr = typeStr+" " + curArgName + "=" + localPackName+"->"+curArgName+";";
                printTabbedLines(out_cfile,assignmentStr);
            }
            // and return
            std::string funcCallStr="";
            if(func->getReturnType()->getTypeID()!=Type::VoidTyID)
            {
                funcCallStr+="*("+localPackName+"->"+generateReturnValName()+")=";
            }
            // call the function
            funcCallStr+=func->getName();
            funcCallStr+="(";
            for(auto argIter = func->arg_begin(); argIter!=func->arg_end(); argIter++)
            {
                if(argIter!=func->arg_begin())
                    funcCallStr+=",";
                funcCallStr+=argIter->getName();
            }
            funcCallStr+=");";
            printTabbedLines(out_cfile,funcCallStr);
            addBarSubTabs(false);
            printTabbedLines(out_cfile,"}");
        }


        void declareInputStructPack()
        {
            std::string structName = generateInputStructType();
            // populate the element
            printTabbedLines(out_cfile,structName);
            printTabbedLines(out_cfile,"{");
            addBarSubTabs(true);
            std::string allArgPackMember="";
            for(auto argIter = func->arg_begin(); argIter!=func->arg_end(); argIter++)
            {
                Argument* curArg = &cast<Argument>(*argIter);
                std::string typeStr = getPackageArgType(curArg);
                std::string nameStr = curArg->getName();
                typeStr+=" "+nameStr;
                typeStr+=";\n";
                allArgPackMember+=typeStr;
            }
            if(func->getReturnType()->getTypeID()!=Type::VoidTyID)
            {
                std::string returnTypePtrStr = getLLVMTypeStr(func->getReturnType())+"*";
                returnTypePtrStr+=" "+generateReturnValName();
                returnTypePtrStr+=";\n";
                allArgPackMember+=returnTypePtrStr;

            }
            printTabbedLines(out_cfile,allArgPackMember);
            addBarSubTabs(false);
            printTabbedLines(out_cfile,"};");
        }

        void generateFunction()
        {
            FuncGenerator::generateFunctionDecl();
            FuncGenerator::generateFunctionBody();
            if(getGeneratingCPU())
            {
                declareInputStructPack();
                generateCPUThreadWrapper();
            }

        }
    };

}


#endif // GENCFUNC_H
