#ifndef GENHLSFIFO_H
#define GENHLSFIFO_H

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
#include <boost/algorithm/string.hpp>
#define FUNCBEGIN  "#FUNCBEGIN:"
#define FUNCEND "#FUNCEND:"
#define FUNCTCLBEGIN "#FUNCTCLBEGIN:"
#define FUNCTCLEND "#FUNCTCLEND:"
#define DIRTCLBEGIN "#DIRECTIVEBEGIN:"
#define DIRTCLEND "#DIRECTIVEEND:"
#define DRIVERBEGIN "#DRIVERBEGIN:"
#define DRIVEREND "#DRIVEREND:"
#define ALLFIFOBEGIN "#FIFOBEGIN:"
#define ALLFIFOEND "#FIFOEND:"

#define FIFORADP_SUFF "radp"
#define FIFOWADP_SUFF "wadp"
#define HDLIPPREFIX "WireleSuns:user:"
namespace GenTCL{



    static std::set<Argument*> arg2axim;
    static std::set<Argument*> arg2fifoW;
    static std::set<Argument*> arg2fifoR;


    // these would only be used when we are generating
    // HLS tcl files


    static std::string standardHLSTcl(std::string funcTop)
    {
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
        actualTcl+="quit\n";
        return actualTcl;

    }

    class VivadoIPGenerator
    {
    public:
        static std::string makeIP(std::string ipName, std::string instanceName, std::string prefix)
        {
            std::string generateIPStr = "create_bd_cell -type ip -vlnv "+prefix+ipName;
            generateIPStr+=" "+instanceName;
            return generateVivadoStartEndGroupStr(generateIPStr);

        }
        static std::string setIPProperty(std::string ipName, std::map<std::string, std::string>& propertyMap)
        {
            std::string setPropertyStr = "set_property -dict [list ";
            for(auto pairIter = propertyMap.begin(); pairIter != propertyMap.end(); pairIter++)
            {
                std::string propertyName = pairIter->first;
                std::string propertyVal = pairIter->second;
                setPropertyStr+="CONFIG."+propertyName;
                setPropertyStr+=" {"+propertyVal+"} ";
            }
            setPropertyStr+="] [get_bd_cells "+ipName+"]";
            return generateVivadoStartEndGroupStr(setPropertyStr);
        }
        static std::string connectIntf(std::string srcIPName, std::string srcPortName, std::string dstIPName, std::string dstPortName)
        {
            std::string conStr = "connect_bd_intf_net [";
            std::string getBdIntfCmd = "get_bd_intf_pins";

            conStr+=getBdIntfCmd+" "+srcIPName+"/"+srcPortName;
            conStr+="] [";

            conStr+=getBdIntfCmd+" "+dstIPName+"/"+dstPortName;
            conStr+="]\n";
            return conStr;
        }
    };


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

        static std::string drivenAutobd(std::string masterName, std::string slaveName, bool master)
        {
            std::string autobdstr = "apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {";
            if(master)
                autobdstr+="Master";
            else
                autobdstr+="Slave";
            autobdstr+=" \"/";
            if(master)
                autobdstr+=masterName;
            else
                autobdstr+=slaveName;
            autobdstr+="\" Clk \"Auto\" }";
            autobdstr+=" [get_bd_intf_pins ";
            if(master)
                autobdstr+=slaveName;
            else
                autobdstr+=masterName;
            autobdstr+="]\n";
            return autobdstr;
        }
        static std::string oneToManyAuto( std::vector<std::string>& masterPorts,
                                                   std::vector<std::string>& slavePorts)
        {
            // we'd do automatic connection
            // if there is only one master, we apply the same master automation multiple times each
            // time with a different slave -- this would all connect them to the same interconnect
            std::string rtStr = "";
            assert(masterPorts.size()>0 && "no master port\n");
            std::string firstMasterPort = *masterPorts.begin();

            if(masterPorts.size()==1)
            {
                for(auto slaveIter = slavePorts.begin(); slaveIter!=slavePorts.end(); slaveIter++)
                {
                    std::string curSlave = *slaveIter;
                    rtStr+=drivenAutobd(firstMasterPort,curSlave,true);
                }
            }
            else if(slavePorts.size()==1)
            {
                // if there are more than one master, but one slave, we would do the master automation
                // for the first master, and then do the slave driven automation for the remaining masters
                std::string slavePort = *slavePorts.begin();
                rtStr+=drivenAutobd(firstMasterPort,slavePort,true);
                for(auto masterIter = masterPorts.begin()+1; masterIter!=masterPorts.end(); masterIter++)
                {
                    std::string curMaster = *masterIter;
                    rtStr+=drivenAutobd(curMaster,slavePort,false);
                }
            }
            else
            {
                llvm_unreachable("N2N interconnect ");
            }
            return rtStr;
        }

        static std::string generateAxiConnectN2N(
                                              std::vector<std::string>& masterPorts,
                                              std::vector<std::string>& slavePorts,
                                              int counter)
        {
            int slaveNum = masterPorts.size();
            int masterNum = slavePorts.size();

            std::string intconIpName = "axi_interconnect:";
            intconIpName+=XILINXINTCONVERSION;
            std::string instanceName = "axi_interconnect_"+boost::lexical_cast<std::string>(counter);

            std::string instantiation = VivadoIPGenerator::makeIP(intconIpName,instanceName,XILINXIPPREFIX);



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



        static std::string generateAxiConnect(
                                              std::vector<std::string>& masterPorts,
                                              std::vector<std::string>& slavePorts,
                                              int counter)
        {
            int slaveNum = masterPorts.size();
            int masterNum = slavePorts.size();

            if(slaveNum>1 && masterNum >1)
                return generateAxiConnectN2N(masterPorts,slavePorts,counter);
            else
                return oneToManyAuto(masterPorts,slavePorts);
        }




    };

    class HLSFifoGenerator
    {
    public:
        HLSFifoGenerator(std::vector<Argument*>* ua)
        {
            userArguments = ua;
        }

        static void generateRstClkConnection(raw_ostream& out)
        {
            out<<"/*"<<ALLFIFOBEGIN<<"\n";
            out<<"connect_bd_net [get_bd_pins /rst_processing_sys7_100M/peripheral_reset] [get_bd_pins */rst]\n";
            out<<"connect_bd_net [get_bd_pins /rst_processing_sys7_100M/slowest_sync_clk] [get_bd_pins */clk]\n";
            out<<ALLFIFOEND<<"*/\n";

        }


        ~HLSFifoGenerator()
        {
            userArguments->clear();
            delete userArguments;
        }
        std::string getFifoName()
        {
            Argument* writer = userArguments->back();
            assert(arg2fifoW.count(writer) && "writer port expected");
            return writer->getName();
        }
        std::string setupFifoStr()
        {
            if(userArguments->size()>2)
            {
                std::string setupFunc = "setup";
                setupFunc+=getFifoName();
                setupFunc+="_dev";
                return setupFunc;
            }
            else
                return "";
        }

        std::string getFifoDataType()
        {
            Argument* writer = userArguments->back();
            assert(writer->getType()->isPointerTy() && "fifo arg is not pointer type\n");
            PointerType* ptype = &cast<PointerType>(*(writer->getType()));
            Type* actualDataType = ptype->getPointerElementType();
            return getLLVMTypeStr(actualDataType);
        }

        void generateForkC(raw_ostream& out)
        {
            std::string forkC ="";
            if(userArguments->size()>2)
            {
                forkC +="//";
                forkC += FUNCBEGIN;
                std::string fifoName = getFifoName();
                forkC+=fifoName;
                forkC+="\n";
                std::string fifoDataName = fifoName+"_data";
                std::string fifoDataType = getFifoDataType();

                forkC+="void ";
                forkC+= fifoName;
                forkC+= "(";

                int argCounter = 0;
                std::string dataDecl=fifoDataType+" "+fifoDataName+";\n";
                std::string readStr="";
                std::string distributeStr="";
                for(auto argIter = userArguments->begin(); argIter!=userArguments->end(); argIter++)
                {
                    Argument* curArg = *argIter;
                    std::string curPortName = curArg->getName();
                    std::string curTypeStr = getLLVMTypeStr(curArg->getType());
                    if(argIter != userArguments->begin())
                        forkC+=",";
                    forkC+= curTypeStr;
                    if(arg2fifoR.count(curArg))
                    {
                        curPortName+= boost::lexical_cast<std::string>(argCounter);
                        distributeStr+="*";
                        distributeStr+=curPortName;
                        distributeStr+="=";
                        distributeStr+=fifoDataName;
                        distributeStr+=";\n";
                        argCounter++;
                    }
                    else
                    {
                        readStr+=fifoDataName;
                        readStr+="= *";
                        readStr+=curPortName;
                        readStr+=";\n";
                    }
                    forkC+=" ";
                    forkC+=curPortName;


                }
                forkC+=")";
                printTabbedLines(out,forkC);
                printTabbedLines(out,"{");
                addBarSubTabs(true);
                printTabbedLines(out, dataDecl);
                printTabbedLines(out,"while(1)");
                printTabbedLines(out,"{");
                addBarSubTabs(true);
                printTabbedLines(out, readStr);
                printTabbedLines(out, distributeStr);
                addBarSubTabs(false);
                printTabbedLines(out,"}");
                addBarSubTabs(false);
                printTabbedLines(out,"}");
                std::string endStr = "//";
                endStr+=FUNCEND;
                printTabbedLines(out,endStr);
            }
        }
        void generateForkTcl(raw_ostream& out)
        {
            std::string synthFork = "";
            if(userArguments->size()>2)
            {
                std::string fifoName = getFifoName();
                synthFork+="/*\n";
                synthFork+=FUNCTCLBEGIN;
                synthFork+="\n";
                std::string synthTcl = standardHLSTcl(fifoName);
                synthFork+= synthTcl;
                synthFork+= FUNCTCLEND;
                synthFork+="\n";
                synthFork+="*/\n";
            }
            printTabbedLines(out,synthFork);

        }
        void generateForkDirTcl(raw_ostream& out)
        {
            std::string dirStr="";
            if(userArguments->size()>2)
            {
                dirStr+="/*\n";
                dirStr+=DIRTCLBEGIN;
                dirStr+="\n";
                // for every port, make it ap_fifo


                std::string directiveStrHead = "set_directive_interface -mode ";
                dirStr+=directiveStrHead;
                dirStr+="s_axilite -register \"";
                dirStr+=getFifoName();
                dirStr+="\"";
                dirStr+="\n";

                std::string fifoIntfDir = directiveStrHead+"ap_fifo \"";
                fifoIntfDir+=getFifoName();
                fifoIntfDir+="\" ";
                int argCounter=0;
                for(auto argIter = userArguments->begin(); argIter!=userArguments->end(); argIter++)
                {
                    Argument* curArg = *argIter;
                    std::string curArgName = curArg->getName();
                    if(arg2fifoR.count(curArg))
                    {
                        curArgName+=boost::lexical_cast<std::string>(argCounter);
                        argCounter++;
                    }
                    dirStr+=fifoIntfDir;
                    dirStr+=curArgName;
                    dirStr+="\n";
                }
                dirStr+=DIRTCLEND;
                dirStr+="\n";
                dirStr+="*/\n";
            }
            printTabbedLines(out, dirStr);

        }
        void generateForkDriver(raw_ostream& out)
        {
            if(userArguments->size()>2)
            {
                printTabbedLines(out,"/*");
                printTabbedLines(out,DRIVERBEGIN);

                std::string deviceName = getFifoName();
                deviceName = "x"+deviceName;
                std::string deviceVarName = getFifoName()+"_dev";
                std::string deviceDec = appendCapXCapFirstLetter(getFifoName())+" "+deviceVarName+";";
                std::string driverHeader= deviceName+".h";
                boost::algorithm::to_lower(driverHeader);
                printTabbedLines(out,"#include \""+driverHeader+"\"");
                printTabbedLines(out,deviceDec);
                std::string setupFuncName = this->setupFifoStr();
                std::string setupDevice="void "+setupFuncName+"()";
                printTabbedLines(out,setupDevice);
                printTabbedLines(out,"{");
                addBarSubTabs(true);
                printTabbedLines(out,"int status="+appendCapXCapFirstLetter(getFifoName())+"_Initialize(&"+deviceVarName+",0);");
                printTabbedLines(out, "if(status!=XST_SUCCESS)xil_printf(\"cannot initialize "+ deviceName +"\");");
                addBarSubTabs(false);
                printTabbedLines(out,"}");
                printTabbedLines(out,DRIVEREND);
                printTabbedLines(out,"*/");
            }
        }

        void generateHLSFifo(raw_ostream& out)
        {
            // more than just tcl, also the c code for distibution
            generateForkC(out);
            errs()<<"done fork\n";
            generateForkTcl(out);
            errs()<<"done tcl\n";
            generateForkDirTcl(out);
            errs()<<"done dir\n";
            generateForkDriver(out);
        }
        int getFifoWidth()
        {
            Argument* writer = userArguments->back();
            assert(writer->getType()->isPointerTy() && "fifo arg is not pointer type\n");
            PointerType* ptype = &cast<PointerType>(*(writer->getType()));
            Type* actualDataType = ptype->getPointerElementType();

            return getLLVMTypeWidth(actualDataType);

        }
        std::string getIPInstanceName(Argument* arg)
        {
            std::string funcName = arg->getParent()->getName();
            return funcName+"_0";
        }
        std::string getIPInstancePortName(Argument* arg)
        {
            unsigned argSeq = arg->getArgNo();
            std::string argName = arg->getName();


            if(arg->getType()->isPointerTy())
            {
                PointerType* pty = &cast<PointerType>(*(arg->getType()));
                if(pty->getElementType()->isIntegerTy())
                    argName+="_V";
            }
            return argName;
            //bool isRdChannel = cmpChannelAttr(arg->getParent()->getAttributes(),argSeq,CHANNELRD);

        }

        std::string makeFifoStr(std::string fifoName, int width, int depth)
        {

            std::string insFifo = VivadoIPGenerator::makeIP("fifo_generator:12.0",fifoName,XILINXIPPREFIX);
            std::map<std::string,std::string> fifoProperty;
            fifoProperty["Fifo_Implementation"] = "Common_Clock_Distributed_RAM";

            if(width*depth>=4096)
            {
                fifoProperty["Fifo_Implementation"] = "Common_Clock_Block_RAM";
            }
            fifoProperty["Performance_Options"] = "First_Word_Fall_Through";
            fifoProperty["Input_Depth"] = boost::lexical_cast<std::string>(depth);
            fifoProperty["Input_Data_Width"] = boost::lexical_cast<std::string>(width);
            fifoProperty["Data_Count"] = "true";
            std::string parameterizeFifo = VivadoIPGenerator::setIPProperty(fifoName,fifoProperty);
            return insFifo+parameterizeFifo;
        }


        std::string makeAndConnectFifoAdapters(std::string fifoName , int width )
        {
            std::string radapterName = fifoName+FIFORADP_SUFF;
            std::string insReadConn = VivadoIPGenerator::makeIP("FIFO_read_conn:1.0",radapterName,HDLIPPREFIX);
            std::map<std::string, std::string> connProperty;
            connProperty["DATA_WIDTH"] = boost::lexical_cast<std::string>(width);
            std::string tuneReadConn = VivadoIPGenerator::setIPProperty(radapterName,connProperty);

            std::string wadapterName = fifoName+FIFOWADP_SUFF;
            std::string insWriteConn = VivadoIPGenerator::makeIP("FIFO_write_conn:1.0",wadapterName,HDLIPPREFIX);
            std::string tuneWriteConn = VivadoIPGenerator::setIPProperty(wadapterName,connProperty);

            // now connect
            std::string connectFifo2wadap = VivadoIPGenerator::connectIntf(fifoName,"FIFO_WRITE",wadapterName,"write_to_fifo_rtl");
            std::string connectFifo2radap = VivadoIPGenerator::connectIntf(fifoName,"FIFO_READ",radapterName,"read_from_fifo_rtl");

            return insReadConn+tuneReadConn+insWriteConn+tuneWriteConn+connectFifo2radap+connectFifo2wadap;

        }

        std::string getSrcFifoName()
        {
            return getFifoName()+"srcFifo";
        }

        void instantiateSourceFifo(raw_ostream& out)
        {
            int fifoWidth = getFifoWidth();
            std::string srcFifoName = getSrcFifoName();
            std::string makeFifo = makeFifoStr(srcFifoName,fifoWidth, 256);
            printTabbedLines(out, makeFifo);
            // now instantiate the connectors
            std::string fifoAdapters = makeAndConnectFifoAdapters(srcFifoName,fifoWidth);
            printTabbedLines(out, fifoAdapters);
        }
        void connectSourceFifo(raw_ostream& out)
        {
            Argument* writer = userArguments->back();
            assert(writer->getType()->isPointerTy() && "fifo arg is not pointer type\n");
            // now we want to figure out which port on which block it is
            std::string srcBlockName = getIPInstanceName(writer);
            std::string srcPortName = getIPInstancePortName(writer);
            // now connect to the source fifo
            std::string srcFifoName = getSrcFifoName();
            std::string srcFifoWAdapter = srcFifoName+FIFOWADP_SUFF;
            std::string srcFifowAdapterWPort = "write_from_hls_acc";
            printTabbedLines(out,VivadoIPGenerator::connectIntf(srcBlockName,srcPortName,srcFifoWAdapter,srcFifowAdapterWPort));
        }
        //-------------------------
        /*void instantiateFork(raw_ostream& out)
        {
            if(userArguments->size()<=2)
                return;
            std::string forkName = getFifoName();
            std::string forkInstanceName = forkName+"_0";
            std::string makeFork = VivadoIPGenerator::makeIP(forkName+":1.0",forkInstanceName,HLSIPPREFIX);
            printTabbedLines(out,makeFork);
        }*/

        //-------------------------
        void instantiateDestFifos(raw_ostream& out)
        {
            if(userArguments->size()<=2)
                return;

            int fifoWidth = getFifoWidth();
            for(int i = 0; i<userArguments->size()-1; i++)
            {
                std::string curDstFifoName = getFifoName()+"dstFifo_"+boost::lexical_cast<std::string>(i);
                std::string makeCurDstFifo = makeFifoStr(curDstFifoName,fifoWidth,32);
                printTabbedLines(out,makeCurDstFifo);
                std::string fifoAdapters = makeAndConnectFifoAdapters(curDstFifoName,fifoWidth);
                printTabbedLines(out, fifoAdapters);
            }
        }
        void connectFork(raw_ostream& out)
        {
            if(userArguments->size()<=2)
                return;
            std::string forkName = getFifoName();
            std::string forkInstanceName = forkName+"_0";
            Argument* writer = userArguments->back();
            Type* writerType = writer->getType();
            PointerType* writerPt = &cast<PointerType>(*writerType);
            // connect fork to srcFifo
            std::string srcFifoAdaptedPort=getSrcFifoName();
            srcFifoAdaptedPort+=FIFORADP_SUFF;
            std::string writerPort = writer->getName();
            if(writerPt->getPointerElementType()->isIntegerTy())
                writerPort+="_V";
            std::string connectFork2SrcFifo =
                    VivadoIPGenerator::connectIntf(forkInstanceName,writerPort,srcFifoAdaptedPort,"read_to_hls_acc");
            printTabbedLines(out,connectFork2SrcFifo);
            // connect fork to dstFifos
            for(int i= 0; i<userArguments->size()-1; i++)
            {
                Argument* curReader = userArguments->at(i);
                std::string curDstFifoName = getFifoName()+"dstFifo_"+boost::lexical_cast<std::string>(i);
                std::string curDstFifoAdaptedPort = curDstFifoName+FIFOWADP_SUFF;

                std::string readerPort = curReader->getName();
                readerPort+=boost::lexical_cast<std::string>(i);
                if(writerPt->getPointerElementType()->isIntegerTy())
                    readerPort+="_V";
                std::string connectFork2DstFifos =
                        VivadoIPGenerator::connectIntf(forkInstanceName,readerPort,curDstFifoAdaptedPort,"write_from_hls_acc");
                printTabbedLines(out, connectFork2DstFifos);
            }

        }

        void connectDestFifos(raw_ostream& out)
        {
            bool singleSrc = (userArguments->size()==2);
            std::string adapterOutputPort = "read_to_hls_acc";
            for(int i =0; i<userArguments->size()-1; i++)
            {
                Argument* curReader = userArguments->at(i);
                std::string dstBlockName = getIPInstanceName(curReader);
                std::string dstBlockPortName = getIPInstancePortName(curReader);
                if(singleSrc)
                {
                    // now connect to the source fifo
                    std::string srcFifoName = getSrcFifoName();
                    std::string srcFifoRAdapter = srcFifoName+FIFORADP_SUFF;
                    printTabbedLines(out,VivadoIPGenerator::connectIntf(srcFifoRAdapter,adapterOutputPort,dstBlockName,dstBlockPortName));
                }
                else
                {
                    // this will be from the dst fifos
                    std::string curDstFifoName = getFifoName()+"dstFifo_"+boost::lexical_cast<std::string>(i);
                    std::string curDstFifoRAdapter = curDstFifoName+FIFORADP_SUFF;
                    printTabbedLines(out,VivadoIPGenerator::connectIntf(curDstFifoRAdapter,adapterOutputPort,dstBlockName,dstBlockPortName));
                }
            }

        }

        void generateFifoInstantiationConnection(raw_ostream& out)
        {
            out<<"/*"<<ALLFIFOBEGIN<<"\n";

            instantiateSourceFifo(out);
            connectSourceFifo(out);
            //instantiateFork(out);

            instantiateDestFifos(out);
            connectFork(out);
            connectDestFifos(out);

            out<<ALLFIFOEND<<"*/\n";
        }


        // things to handle in fifo generation
        // read
        std::vector<Argument*>* userArguments;
    };




    class HLSTopLevelGenerator
    {
    public:
        HLSTopLevelGenerator()
        {
        }
        void addFifoGen(HLSFifoGenerator* fg)
        {
            allFifoGens.push_back(fg);
        }
        static void generateIncludeSegment(raw_ostream& out)
        {
            out<<"/*"<<ALLFIFOBEGIN<<"\n";
            out<<"include_bd_addr_seg [get_bd_addr_segs -excluded */SEG_processing_sys7_ACP_M_AXI_GP0]\n";
            out<<"include_bd_addr_seg [get_bd_addr_segs -excluded */SEG_processing_sys7_ACP_IOP]\n";
            out<<ALLFIFOEND<<"*/\n";
        }
        static void generateClkFreqIncrease(raw_ostream& out)
        {
            std::map<std::string,std::string> clkPro;
            clkPro["PCW_FPGA0_PERIPHERAL_FREQMHZ"]="125.000000";
            std::string setClk = VivadoIPGenerator::setIPProperty("processing_sys7",clkPro);
            out<<"/*"<<ALLFIFOBEGIN<<"\n";
            out<<setClk<<"\n";
            out<<ALLFIFOEND<<"*/\n";
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

            std::string psIpName = HLSPSIPName;
            psIpName+=":";
            psIpName+=HLSPSIPVersion;
            std::string instPS = GenTCL::VivadoIPGenerator::makeIP(psIpName,HLSPSIPInstName,"");

            instPS+="apply_bd_automation -rule ";
            instPS+=HLSBDRULE;
            instPS+=" [get_bd_cells ";
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
                std::string coreInstanceName = functionName+"_0";

                instantiateCores+=VivadoIPGenerator::makeIP(functionName+":1.0",coreInstanceName,HLSIPPREFIX);
                slavePorts.push_back(coreInstanceName+"/s_axi_AXILiteS");

            }
            // also there would be whole bunch of distributor fifo things
            // we check the fifo
            for(auto fifoGenIter = allFifoGens.begin(); fifoGenIter!=allFifoGens.end(); fifoGenIter++)
            {
                HLSFifoGenerator* fg = *fifoGenIter;
                std::vector<Argument*>* allInvolved =  fg->userArguments;
                if(allInvolved->size()>2)
                {
                    std::string fifoDistCoreName = allInvolved->back()->getName();
                    std::string fifoDistCoreInstanceName = fifoDistCoreName+"_0";
                    instantiateCores+=VivadoIPGenerator::makeIP(fifoDistCoreName+":1.0",fifoDistCoreInstanceName,HLSIPPREFIX);
                    slavePorts.push_back(fifoDistCoreInstanceName+"/s_axi_AXILiteS");
                }
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

            if( arg2axim.size()!=0)
            {
                connectAxiM+="set_property -dict [list CONFIG.PCW_USE_S_AXI_ACP {1} CONFIG.PCW_USE_DEFAULT_ACP_USER_VAL {1}] [get_bd_cells ";
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
                setupStr += functionName+"_dev(";

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
            // instantiate the timer, and put it in front of everything....
            std::string timerInstantiation="";
            if(addMeasuringTime)
            {
                timerInstantiation += "#define TIMER_LOAD_VALUE 0xFFFFFFFF;\n";
                timerInstantiation += "#define TIMER_DEVICE_ID XPAR_SCUTIMER_DEVICE_ID;\n";
                timerInstantiation += "XScuTimer Timer;\n";
                timerInstantiation += "volatile u32 CntValue;\n";
                timerInstantiation += "XScuTimer_Config *ConfigPtr;\n";
                timerInstantiation += "XScuTimer* TimerInstancePtr;\n";
                timerInstantiation += "TimerInstancePtr = &Timer;\n";
                timerInstantiation += "ConfigPtr = XScuTimer_LookupConfig(TIMER_DEVICE_ID);\n";
                timerInstantiation += "if(!ConfigPtr)xil_printf(\"scutimer cannot be found\\n\");\n";
                timerInstantiation += "int Status = XScuTimer_CfgInitialize(TimerInstancePtr,ConfigPtr, ConfigPtr->BaseAddr);\n";
                timerInstantiation += "if(Status!=XST_SUCCESS) xil_printf(\"scutimer initialization fail\\n\");\n ";
                timerInstantiation += "XScuTimer_LoadTimer(TimerInstancePtr, TIMER_LOAD_VALUE);\n";
                timerInstantiation += "XScuTimer_Start(TimerInstancePtr);\n";
            }
            // start each stage
            std::string startAll = "";
            std::string checkDone = "";

            for(auto stageIter = hlsCores.begin(); stageIter!=hlsCores.end(); stageIter++)
            {
                CallInst* curStage = *stageIter;
                Function* curStageFunc = curStage->getCalledFunction();
                std::string devName = generateDeviceName(curStageFunc);
                std::string devVarName = generateDeviceVarName(curStageFunc);
                std::string startDev = devName+"_Start(&";
                startDev+=devVarName;
                startDev+=");\n";
                startAll+=startDev;

                std::string checkCurStage = "while(!";
                checkCurStage += devName+"_IsDone(&";
                checkCurStage += devVarName+"));\n";


                checkDone += checkCurStage;
            }
            if(addMeasuringTime)
            {
                checkDone+="CntValue = XScuTimer_GetCounterValue(TimerInstancePtr);\n";
                checkDone+="printf(\"time: %d clock cycles\\n\\r\",TIMER_LOAD_VALUE-CntValue );\n";
            }
            return timerInstantiation+startAll+checkDone;
        }

    private:
        std::vector<CallInst*> hlsCores;
        std::vector<HLSFifoGenerator*> allFifoGens;

    };
}

#endif // GENHLSFIFO_H
