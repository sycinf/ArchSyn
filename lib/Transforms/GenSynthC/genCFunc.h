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
#include "llvm/Transforms/GenSynthC/GenSynthC.h"
#include "GenCInsn.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <iterator>
using namespace llvm;
using namespace std;
#define FUNCBEGIN  "#FUNCBEGIN:"
#define FUNCEND "#FUNCEND:"
#define FUNCTCLBEGIN "#FUNCTCLBEGIN:"
#define FUNCTCLEND "#FUNCTCLEND:"
#define DIRTCLBEGIN "#DIRECTIVEBEGIN:"
#define DIRTCLEND "#DIRECTIVEEND:"
#define DRIVERBEGIN "#DRIVERBEGIN:"
#define DRIVEREND "#DRIVEREND:"
namespace GenCFunc {

    // this two keeps track of if argument should be
    // turned into axi master or fifo ports
    // these would only be used when we are generating
    // HLS tcl files
    static std::set<Argument*> arg2axim;
    static std::set<Argument*> arg2fifoW;
    static std::set<Argument*> arg2fifoR;

    class AxiInterconnectGenerator{
    public:


        static std::string generateSingleInterfaceConnection(std::string master, std::string slave)
        {
            std::string connectInterfaces = "";
            connectInterfaces+="connect_bd_intf_net -boundary_type upper ";
            connectInterfaces+="[get_bd_intf_pins ";
            connectInterfaces+=master;
            connectInterfaces+="] ";
            connectInterfaces+="[get_bd_intf_pins ";
            connectInterfaces+=slave;
            connectInterfaces+="]\n";
            return connectInterfaces;

        }

        static std::string generateAxiConnect(
                                              std::vector<std::string>& masterPorts,
                                              std::vector<std::string>& slavePorts,
                                              int counter)
        {
            int slaveNum = masterPorts.size();
            int masterNum = slavePorts.size();
            std::string instantiation="create_bd_cell -type ip -vlnv ";
            instantiation+=XILINXIPPREFIX;
            instantiation+="axi_interconnect:";
            instantiation+=XILINXINTCONVERSION;
            instantiation+=" ";
            std::string instanceName = "axi_interconnect_"+boost::lexical_cast<std::string>(counter);

            instantiation+=instanceName;
            instantiation=generateVivadoStartEndGroupStr(instantiation);
            std::string setPortNum = "set_property -dict [list CONFIG.NUM_SI {"+
                    boost::lexical_cast<std::string>(slaveNum)
                    +"} CONFIG.NUM_MI {"+
                    boost::lexical_cast<std::string>(masterNum)
                    +"}] [get_bd_cells "+instanceName+"]\n";
            // masterPorts are connected to slave ports of the instantiated axi
            std::string connectInterfaces="";
            int axiSlavePortCounter = 0;
            for(auto masterPortIter = masterPorts.begin(); masterPortIter!= masterPorts.end(); masterPortIter++ )
            {
                std::string curMasterPort = *masterPortIter;
                std::string axiSlavePortStr =instanceName+"/S";
                if(axiSlavePortCounter<10)
                    axiSlavePortStr+="0";
                axiSlavePortStr+=boost::lexical_cast<std::string>(axiSlavePortCounter);
                axiSlavePortCounter++;
                axiSlavePortStr+="_AXI";

                connectInterfaces+=generateSingleInterfaceConnection(curMasterPort,axiSlavePortStr);
            }
            int axiMasterPortCounter = 0;
            for(auto slavePortIter=slavePorts.begin(); slavePortIter!=slavePorts.end(); slavePortIter++)
            {
                std::string curSlavePort = *slavePortIter;
                std::string axiMasterPortStr = instanceName+"/M";
                if(axiMasterPortCounter<10)
                    axiMasterPortStr+="0";
                axiMasterPortStr+=boost::lexical_cast<std::string>(axiMasterPortCounter);
                axiMasterPortCounter++;
                axiMasterPortStr+="_AXI";
                connectInterfaces+=generateSingleInterfaceConnection(curSlavePort,axiMasterPortStr);
            }
            return instantiation+setPortNum+connectInterfaces;
        }


    };

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
            // this is a line another script can parse and extract function
            std::string tagLine = "//";
            tagLine += FUNCBEGIN;
            tagLine += func->getName();
            printTabbedLines(out_cfile,tagLine);
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
                // now add volatile if necessary
                if((curFuncArg->hasNoCaptureAttr() || argIsChannel) && !getGeneratingCPU())
                {
                    curArgTypeStr = "volatile "+curArgTypeStr;
                }

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
            std::string endTagLine = "//";
            endTagLine+=FUNCEND;
            printTabbedLines(out_cfile,endTagLine);
        }

    };


    class HLSTopLevelGenerator
    {
    public:
        HLSTopLevelGenerator()
        {
        }
        std::string generateDefaultTopLevelTcl()
        {
            // this is to instantiate the default PS ip
            std::string commonDir =  HLSDIRVARNAME;
            std::string createPro = "create_project project_1 $"+commonDir+"/vivado/project_1 -part ";
            createPro+=HLSFPGA;
            createPro+="\n";
            createPro+="set_property board_part ";
            createPro+=HLSZEDBOARD;
            createPro+= " [current_project]\n";
            createPro+= "create_bd_design \"design_1\"\n";

            std::string instPS = "create_db_cell -type ip -vlnv ";
            instPS+=HLSPSIPName;
            instPS+=":";
            instPS+=HLSPSIPVersion;
            instPS+=" ";
            instPS+=HLSPSIPInstName;

            instPS = generateVivadoStartEndGroupStr(instPS);
            instPS+="apply_bd_automation ";
            instPS+=HLSBDRULE;
            instPS+="[get_bd_cells ";
            instPS+=HLSPSIPInstName;
            instPS+="]\n";
            // now got to set IPRepo path

            std::string setIpRepoPath = "set_property ip_repo_paths $";
            setIpRepoPath+= commonDir+"/vivado_hls [current_project]\n";
            setIpRepoPath+="update_ip_catalog\n";

            // now's the part where all the stage ipcores are instantiated
            // for that we'll need the name of the called functions
            //FIXME: add the code to instantiate cores
            std::string instantiateCores="";
            //std::vector<std::string> coreInstances;
            std::vector<std::string> slavePorts;
            for(auto coreIter = hlsCores.begin(); coreIter!=hlsCores.end(); coreIter++)
            {
                CallInst* curCallInst = *coreIter;
                std::string functionName = curCallInst->getCalledFunction()->getName();

                std::string generateCurCore = "create_bd_cell -type ip -vlnv ";
                generateCurCore+=HLSIPPREFIX;
                generateCurCore+=functionName+":1.0";
                generateCurCore+=" ";
                std::string coreInstanceName = functionName+"_0";

                generateCurCore+=coreInstanceName;

                slavePorts.push_back(coreInstanceName+"/s_axi_AXILiteS");

                instantiateCores+=generateVivadoStartEndGroupStr(generateCurCore);
            }
            // the master port will be the processing system's gp port
            std::string controlMasterPort = HLSPSIPInstName;
            controlMasterPort+="/M_AXI_GP0";
            std::vector<std::string> masterPorts;
            masterPorts.push_back(controlMasterPort);
            int axiCounter = 0;
            // the control interconnect
            std::string controlAxiInterconnect = AxiInterconnectGenerator::generateAxiConnect(masterPorts,slavePorts,axiCounter);
            axiCounter++;
            std::string connectAxiM="";
            // this is the part where we connect to aximaster -- we do first ACP
            // and the IP masterPorts with the name "m_axi_"+argumentName
            std::vector<std::string> maxiMasterPorts;

            if(arg2axim.size()!=0)
            {
                connectAxiM+="set_property -dict [list CONFIG.PCW_USE_S_AXI_ACP {1}] [get_bd_cells ";
                connectAxiM+=HLSPSIPInstName;
                connectAxiM+="]\n";
                for(auto arg2aximIter = arg2axim.begin(); arg2aximIter!=arg2axim.end(); arg2aximIter++)
                {
                    Argument* curArg = *arg2aximIter;
                    std::string funcName = curArg->getParent()->getName();
                    std::string instanceName = funcName+"_0";
                    std::string portName = "m_axi_";
                    portName+=curArg->getName();

                    std::string completePortName = instanceName+"/"+portName;
                    maxiMasterPorts.push_back(completePortName);
                }
            }
            std::vector<std::string> maxiSlavePorts;
            std::string acpStr = HLSPSIPInstName;
            acpStr += "/S_AXI_ACP";
            maxiSlavePorts.push_back(acpStr);


            std::string maxiInterconnect = AxiInterconnectGenerator::generateAxiConnect(maxiMasterPorts, maxiSlavePorts, axiCounter);


            return createPro+instPS+setIpRepoPath+instantiateCores+controlAxiInterconnect+
                    connectAxiM+maxiInterconnect;


        }
        void addStageFunction(CallInst* stageFunc)
        {
            hlsCores.push_back(stageFunc);
        }
        std::string setupCores()
        {
            std::string setupStr = "";
            for(auto funcIter= hlsCores.begin(); funcIter!=hlsCores.end();funcIter++)
            {
                setupStr+="setup";
                CallInst* curCallInst = *funcIter;
                std::string functionName = curCallInst->getCalledFunction()->getName();
                setupStr += functionName+"(";

                // do the setup -- we check the argument of the called function
                // if it is using Argument from a higher level function, we pass it in
                bool addedPrevArg = false;
                for(auto opIter = curCallInst->op_begin(); opIter!=curCallInst->op_end(); opIter++)
                {
                    Value* curOperand = *opIter;
                    if(isa<Argument>(*curOperand))
                    {
                        Argument* curArg = &cast<Argument>(*curOperand);
                        std::string argName = curArg->getName();
                        if(addedPrevArg)
                            setupStr+=",";
                        else
                            addedPrevArg = true;
                        setupStr+=argName;
                    }
                }
                setupStr +=");\n";
            }
            return setupStr;
        }
        std::string runCores(bool addMeasuringTime)
        {
            //FIXME: got to make the actual start string
            return "";
        }

    private:
        std::vector<CallInst*> hlsCores;

    };
    class HLSFifoGenerator
    {
    public:
        HLSFifoGenerator(std::vector<Argument*>* ua)
        {
            userArguments = ua;
        }

        ~HLSFifoGenerator()
        {
            userArguments->clear();
            delete userArguments;
        }

        std::string generateHLSFifoTcl()
        {
            //FIXME: need to instantiate FIFO and connect them
            return "";
        }
    private:
        // things to handle in fifo generation
        // read
        std::vector<Argument*>* userArguments;
    };

    class PipelinedCFuncGenerator:FuncGenerator{
    public:
        PipelinedCFuncGenerator(Function* f, raw_ostream& os):FuncGenerator(f,os)
        {
            if(!getGeneratingCPU())
            {
                hlsTG = new HLSTopLevelGenerator();
            }
        }
        void generateChannelAllocaStageExec()
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
                else
                {
                    // hls: we need to connect all the pipeline stages....
                    // all the tcl stuff for connecting things
                    // this include
                    if(isa<AllocaInst>(*insIter))
                    {
                        // declare the channel object
                        AllocaInst* ai = &(cast<AllocaInst>(*insIter));
                        // we will need to figure out who is writing to this
                        // fifo, who is reading from it, and give it to the HLS
                        // fifo gen -- note both the reader and the writer
                        // will CallInst, so we will need to figure out the
                        // argument in the function -- so later we can
                        // make the connection properly
                        std::vector<Argument*>* userArguments = new std::vector<Argument*>();
                        for(auto userIter = ai->user_begin(); userIter!= ai->user_end(); userIter++)
                        {
                            if(isa<CallInst>(*userIter))
                            {
                                CallInst* curCallInst = &cast<CallInst>(**userIter);
                                // check seq of the arg operand
                                // and then figure out the argument
                                auto invokedFuncArgIter = curCallInst->getCalledFunction()->arg_begin();
                                for(auto operandIter = curCallInst->op_begin();
                                        operandIter!=curCallInst->op_end() ;operandIter++, invokedFuncArgIter++)
                                {
                                    if(*operandIter==ai)
                                    {
                                        Argument* involvedArg = &cast<Argument>(*invokedFuncArgIter);
                                        userArguments->push_back(involvedArg);
                                        errs()<<involvedArg->getName()<<"\n";
                                    }
                                }
                            }
                        }
                        HLSFifoGenerator* curFifoGen = new HLSFifoGenerator(userArguments);
                        hlsFifoG.push_back(curFifoGen);

                        specialExclude[insIter]="";
                    }
                    else if (isa<CallInst>(*insIter))
                    {
                        CallInst* curCallInst = &cast<CallInst>(*insIter);

                        // give the function name to the hls top level generator
                        hlsTG->addStageFunction(curCallInst);

                        specialExclude[insIter]="";
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
            else
            {
                // make all the driver stuff -----
                // this include the timing measurement stuff?
                // all that would be placed in prefix
                std::string setupStr = hlsTG->setupCores();
                std::string runStr = hlsTG->runCores(false);
                FuncGenerator::bodyPrefixStr +=setupStr;
                FuncGenerator::bodyPrefixStr +="\n";
                FuncGenerator::bodyPrefixStr += runStr;
                //
            }
        }
        void generateHLSTopLevelInComment()
        {
            // create the cores -- the stages
            // should be a set of standard libraries
            // assume common_anc_dir is defined
            // also these cores, if they have axim connection
            // to memory, they will need to be connected to axi interconnect
            std::string hlsTopLevel = hlsTG->generateDefaultTopLevelTcl();

            std::string fifoInst ="";
            // create the fifos -- and connect to the cores by doing all the look up
            for(auto fifoGenIter = this->hlsFifoG.begin(); fifoGenIter!= hlsFifoG.end(); fifoGenIter++)
            {
                HLSFifoGenerator* curFifoGen = *fifoGenIter;
                fifoInst+=curFifoGen->generateHLSFifoTcl();
            }

            std::string directiveBegin="/*\n";
            directiveBegin+=DIRTCLBEGIN;
            printTabbedLines(out_cfile,directiveBegin);
            printTabbedLines(out_cfile,hlsTopLevel);
            printTabbedLines(out_cfile,fifoInst);
            std::string directiveEnd=DIRTCLEND;
            directiveEnd+="\n*/";
            printTabbedLines(out_cfile,directiveEnd);


        }

        void generateFunction()
        {
            FuncGenerator::generateFunctionDecl();
            generateChannelAllocaStageExec();
            FuncGenerator::generateFunctionBody();
            if(!getGeneratingCPU())
            {
                generateHLSTopLevelInComment();
            }

        }
    private:
        // we need a HLS top level generator
        // a vector of fifo generator for each channel
        // the actual function body then just consists of
        // initializing every stage, and start them
        // note the alloca instructions are all replaced
        // with nothing, the call instructions are replaced
        // with core_setup call and core start call
        HLSTopLevelGenerator* hlsTG;
        std::vector<HLSFifoGenerator*> hlsFifoG;
    };
    class NormalCFuncGenerator:FuncGenerator{


        std::set<Argument*> funcArg2BeInitialized;
        // this is specifically for those memory accessing
        // argument -- we need to supply the offset
        std::set<Argument*> funcArg2BeInitialized_offset;


        std::string generateInputStructType()
        {
            int funcSeqNum = getFuncSeq(this->func);
            return generateInputPackStructType(funcSeqNum);
        }

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
        void generateHLSDirectiveInComment()
        {
            std::string directiveBegin="/*\n";
            directiveBegin+=DIRTCLBEGIN;
            std::string funcName = func->getName();
            printTabbedLines(out_cfile,directiveBegin);
            // when an argument is used to generate some new
            // pointer, and then dereferenced, the argument
            std::set<Argument*> dereferenced;
            for(auto bbIter = func->begin(); bbIter!=func->end(); bbIter++)
            {
                BasicBlock* curBB = &(cast<BasicBlock>(*bbIter));
                for(auto insIter = curBB->begin(); insIter!=curBB->end(); insIter++)
                {
                    Instruction& curIns = cast<Instruction>(*insIter);
                    Value* pointerForRef = 0;
                    if(isa<LoadInst>(curIns))
                    {
                        LoadInst& curLdInst = cast<LoadInst>(curIns);
                        pointerForRef = curLdInst.getPointerOperand();
                    }
                    else if(isa<StoreInst>(curIns))
                    {
                        StoreInst& curStInst = cast<StoreInst>(curIns);
                        pointerForRef = curStInst.getPointerOperand();
                    }
                    // FIXME: if the pointer is passed around between
                    // partitions, its unsynthesizable in vivado hls
                    // because the pointer of pointer is not supported,
                    // the fix is to have the decoupling pass grouping
                    // gep instruction and memory reference together...
                    // if there is a chain of gep leading up to the memory
                    // ref, we group them all into the same node
                    // since the pointer wont be passed between partitions,
                    // we can be sure this following snippet gets the argument properly
                    if(pointerForRef)
                    {
                        //search backward -- if it is a Gep instruction then we continue
                        //else we check if it is Argument -- if yes, we have it, else we break
                        //-- now consider the case when we have an argument which we take in
                        // a pointer, we need to add an explicit port for access
                        bool found = isa<Argument>(*pointerForRef);
                        bool isGep = isa<GetElementPtrInst>(*pointerForRef);
                        while(!found && isGep)
                        {
                            GetElementPtrInst& curGepInst = cast<GetElementPtrInst>(*pointerForRef);
                            pointerForRef = curGepInst.getPointerOperand();
                            found = isa<Argument>(*pointerForRef);
                            isGep = isa<GetElementPtrInst>(*pointerForRef);
                        }
                        if(found)
                        {
                            Argument* curDerefPter = &cast<Argument>(*pointerForRef);
                            if(curDerefPter->getParent()==func)
                                dereferenced.insert(curDerefPter);
                        }
                    }
                }

            }
            std::string directiveStrHead = "set_directive_interface -mode ";
            // now we have an array of argument which are dereferenced in the function
            for(auto argIter = dereferenced.begin(); argIter!= dereferenced.end(); argIter++)
            {
                Argument* curArg = *argIter;
                std::string argumentName = curArg->getName();
                // check if this argument is a fifo or axi_master
                unsigned argSeq = curArg->getArgNo();
                bool isWrChannel = cmpChannelAttr(func->getAttributes(),argSeq,CHANNELWR);
                bool isRdChannel = cmpChannelAttr(func->getAttributes(),argSeq,CHANNELRD);
                std::string directiveStr = directiveStrHead;
                if(isWrChannel || isRdChannel)
                {
                    directiveStr+="ap_fifo ";
                    if (isWrChannel)
                        arg2fifoW.insert(curArg);
                    else
                        arg2fifoR.insert(curArg);
                }
                else // we can adjust for many cache by not having separate bundles
                {
                    directiveStr+= " m_axi -offset slave -bundle "+argumentName;
                    funcArg2BeInitialized_offset.insert(curArg);
                    arg2axim.insert(curArg);
                }
                directiveStr+=" \""+funcName+"\" "+argumentName;
                printTabbedLines(out_cfile, directiveStr);
            }
            // make all the other argument input
            for(auto argIter = func->arg_begin(); argIter!=func->arg_end(); argIter++)
            {
                Argument* curArg = &cast<Argument>(*argIter);
                if(dereferenced.count(curArg))
                    continue;
                funcArg2BeInitialized.insert(curArg);
                std::string directiveStr = directiveStrHead;
                directiveStr+=" s_axilite -register \""+funcName+"\" ";
                directiveStr+=curArg->getName();
                printTabbedLines(out_cfile,directiveStr);
            }
            // lastly, add the control port for the whole thing
            std::string entityControlDir = directiveStrHead;
            entityControlDir+= " s_axilite -register \""+funcName+"\"";
            printTabbedLines(out_cfile,entityControlDir);

            std::string directiveEnd=DIRTCLEND;
            directiveEnd+="\n*/";
            printTabbedLines(out_cfile,directiveEnd);
        }

        void generateHLSTclInComment()
        {
            //generate tcl start tagline
            std::string tclBegin = "/*\n";
            tclBegin+=FUNCTCLBEGIN;
            printTabbedLines(out_cfile,tclBegin);
            std::string funcTop = func->getName();
            std::string actualTcl = "open_project "+funcTop+"\n";
            actualTcl+="set_top "+funcTop+"\n";
            actualTcl+="add_files "+funcTop+".cpp\n";
            actualTcl+="open_solution \"solution1\"\n";

            actualTcl+="set_part {";
            actualTcl+=HLSFPGA;
            actualTcl+="}\n";

            actualTcl+="create_clock -period ";
            actualTcl+=HLSFPGA_CLKPERIOD;
            actualTcl+=" -name default\n";
            actualTcl+="set_clock_uncertainty ";
            actualTcl+=HLSFPGA_CLKUNCERTAIN;
            actualTcl+="\n";

            actualTcl+="config_compile -name_max_length 60 -pipeline_loops 1\n";

            actualTcl+="source \"./directive.tcl\"\n";
            actualTcl+="csynth_design\n";
            actualTcl+="export_design -format ip_catalog\n";
            printTabbedLines(out_cfile,actualTcl);

            std::string tclEnd =  FUNCTCLEND;
            tclEnd+="\n*/";
            printTabbedLines(out_cfile,tclEnd);
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
        void generateCurFunctionDriver()
        {

            // for normal function, the driver consists of
            // the standard instantiation of various things
            printTabbedLines(out_cfile,"/*");
            printTabbedLines(out_cfile,DRIVERBEGIN);

            std::string deviceName = "X";
            std::string funcName=func->getName();
            deviceName+=funcName;
            deviceName.at(1)=toupper(deviceName.at(1));
            std::string deviceVarName = funcName+"_dev";
            std::string deviceDec = deviceName+" "+deviceVarName+";";

            std::string driverHeader= deviceName+".h";
            boost::algorithm::to_lower(driverHeader);
            printTabbedLines(out_cfile,"#include \""+driverHeader+"\"");

            printTabbedLines(out_cfile,deviceDec);

            std::string setupDevice="void setup"+funcName+"_dev(";
            // need to figure out the argument used for intializing
            // we have kept track of those worthy of initializing when
            // we were generating the tcl file for creating accelerator
            // interface
            std::string argumentStr="";
            for(auto argIter = func->arg_begin(); argIter!=func->arg_end();argIter++ )
            {
                Argument* curArg = &cast<Argument>(*argIter);
                if(!funcArg2BeInitialized.count(curArg) && !funcArg2BeInitialized_offset.count(curArg))
                    continue;
                if(argumentStr!="")
                    argumentStr+=",";
                argumentStr+=getLLVMTypeStr(curArg->getType(),true);
                argumentStr+=" ";
                argumentStr+=curArg->getName();
            }
            setupDevice+=argumentStr+")";
            printTabbedLines(out_cfile,setupDevice);
            printTabbedLines(out_cfile,"{");
            addBarSubTabs(true);

            printTabbedLines(out_cfile,"int status="+deviceName+"_Initialize(&"+deviceVarName+",0);");
            printTabbedLines(out_cfile, "if(status!=XST_SUCCESS)xil_printf(\"cannot initialize "+ deviceName +"\");");
            std::set<Argument*> all2BeInited;
            all2BeInited.insert(funcArg2BeInitialized.begin(),funcArg2BeInitialized.end());
            all2BeInited.insert(funcArg2BeInitialized_offset.begin(),funcArg2BeInitialized_offset.end());

            for(auto argPtrIter = all2BeInited.begin();
                argPtrIter!=all2BeInited.end();
                argPtrIter++)
            {
                Argument* initArg = *argPtrIter;
                std::string setDevArgFn = deviceName+"_Set_";
                setDevArgFn+=initArg->getName();
                if(funcArg2BeInitialized_offset.count(initArg))
                    setDevArgFn+="_offset";
                setDevArgFn+="(&"+deviceVarName+","+"(u32)";
                setDevArgFn+=initArg->getName();
                setDevArgFn+=");";
                printTabbedLines(out_cfile,setDevArgFn);
            }

            addBarSubTabs(false);
            printTabbedLines(out_cfile,"}");
            printTabbedLines(out_cfile,DRIVEREND);
            printTabbedLines(out_cfile,"*/");

        }


    public:
        NormalCFuncGenerator(Function* f, raw_ostream& os):FuncGenerator(f,os){}


        void generateFunction()
        {
            FuncGenerator::generateFunctionDecl();
            FuncGenerator::generateFunctionBody();
            if(getGeneratingCPU())
            {
                declareInputStructPack();
                generateCPUThreadWrapper();
            }
            else
            {
                // generate the tcl script for generating the accelerator
                // in vivado hls -- the tcl script is in the comment
                generateHLSTclInComment();
                // directive tcl, dealing with how to specify each memory interface
                // we check each memory read/write port, if they are not CHANNELWR/CHANNELRD
                // then they are axi master memory interface, else they are fifo
                generateHLSDirectiveInComment();
                // generate driver for snippet driving this accelerator
                // it consists of initialize the core, and write things into the control
                // registers -- note for the top level (pipelineFunction), we just need
                // to call each of these function, and poll for each to finish
                generateCurFunctionDriver();


            }

        }
    };

}


#endif // GENCFUNC_H
