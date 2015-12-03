#ifndef GENCINSN_H
#define GENCINSN_H
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

#include <iterator>
using namespace llvm;
using namespace std;
namespace GenCFunc {
    class InstructionGenerator{
        Instruction* insn;
        std::vector<std::string>* topVarDecl;
        std::vector<std::string>* bbContent;
        std::map<BasicBlock*, std::vector<std::string>*>* phiPreAssign;
    private:
        std::string generateOperandStr(Value* operand)
        {
            std::string operandStr="";
            if(isa<Argument>(*operand))
            {
                // it's a function argument, so the string will be myVarName = argument name
                operandStr += operand->getName();
            }
            else if(isa<Constant>(*operand))
            {
                // either an instruction or constant
                operandStr += generateConstantStr(cast<Constant>(*operand));
            }
            else
            {
                assert(isa<Instruction>(*operand) && "phi node value operand not argument, constant, or instruction");
                Instruction* operandIns = &(cast<Instruction>(*operand));
                operandStr += generateVariableName(operandIns);
            }
            return operandStr;
        }

        std::string generateVarDecl()
        {
            std::string rtStr="";
            if(!insn->isTerminator() && insn->getType()->getTypeID()!=Type::VoidTyID)
            {
                std::string rtVarName = generateVariableName(insn);
                std::string varType=getLLVMTypeStr(insn->getType());
                if(isa<GetElementPtrInst>(*insn))
                {
                    GetElementPtrInst* curGep = &cast<GetElementPtrInst>(*insn);
                    Value* pointerVal = curGep->getPointerOperand();
                    bool foundArg = isa<Argument>(*pointerVal);
                    bool isGepIns = isa<GetElementPtrInst>(*pointerVal);
                    while(!foundArg && isGepIns)
                    {
                        curGep = &cast<GetElementPtrInst>(*pointerVal);
                        pointerVal = curGep->getPointerOperand();
                        foundArg = isa<Argument>(*pointerVal);
                        isGepIns = isa<GetElementPtrInst>(*pointerVal);
                    }
                    if(foundArg)
                    {
                        Argument* curArg = &cast<Argument>(*pointerVal);
                        if(curArg->hasNoCaptureAttr())
                            varType= "volatile "+varType;
                    }

                }
                rtStr = varType+" "+rtVarName+";";
            }
            return rtStr;
        }

        void generatePhiNode()
        {
            PHINode&  curPhi = cast<PHINode>(*insn);
            std::string myVarName = generateVariableName(insn);

            for(unsigned int inBlockInd = 0; inBlockInd<curPhi.getNumIncomingValues();inBlockInd++)
            {
                BasicBlock* curPred = curPhi.getIncomingBlock(inBlockInd);
                Value* curPredVal = curPhi.getIncomingValueForBlock(curPred);
                std::string assignmentStr = myVarName;
                std::string assignValueStr = generateOperandStr(curPredVal);

                assignmentStr += " = ";
                assignmentStr += assignValueStr;
                assignmentStr += ";";
                // now we check if the BB has a vector of strings, if not we add one
                if(phiPreAssign->find(curPred)==phiPreAssign->end())
                {
                    std::vector<std::string>* predPhiStrings = new std::vector<std::string>();
                    phiPreAssign->insert(make_pair(curPred,predPhiStrings));
                }
                phiPreAssign->at(curPred)->push_back(assignmentStr);
            }
        }
        void generateBranch()
        {
            BranchInst& bi = cast<BranchInst>(*insn);
            BasicBlock* firstSuc = bi.getSuccessor(0);
            std::string firstSucName = firstSuc->getName();
            std::string branchStr ="";
            if(bi.isUnconditional() )
            {
                branchStr += "goto ";
                branchStr += firstSucName;
                branchStr +=";";
            }
            else
            {
                Value* condVal = bi.getCondition();
                assert( (isa<Instruction>(*condVal) || isa<Argument>(*condVal))  && "conditional branch with conditional var not arg nor ins");
                std::string switchVar=generateOperandStr(condVal);
                branchStr+="if(";
                branchStr+=switchVar;
                branchStr+=") goto ";
                branchStr+=firstSucName;
                branchStr+=";\n";

                branchStr+="else goto ";
                branchStr+=bi.getSuccessor(1)->getName();
                branchStr+=";\n";
            }
            bbContent->push_back(branchStr);
        }
        void generateReturn()
        {
            ReturnInst& ri = cast<ReturnInst>(*insn);
            std::string returnStr = "return ";
            if(ri.getReturnValue()==0)
                returnStr = returnStr+";\n";
            else
            {
                Value* retVal = ri.getReturnValue();
                returnStr+=generateOperandStr(retVal);
                returnStr+=";\n";
            }
            bbContent->push_back(returnStr);
        }
        void generateSwitch()
        {
            SwitchInst& si = cast<SwitchInst>(*insn);
            std::string switchName = generateOperandStr(si.getCondition());
            errs()<<switchName<<"\n";
            std::string switchStr ="switch(";
            switchStr+= switchName;
            switchStr+= "){\n";
            for(unsigned int sucInd=0; sucInd < si.getNumSuccessors(); sucInd++)
            {
                BasicBlock* curBB = si.getSuccessor(sucInd);
                ConstantInt* curCaseVal = si.findCaseDest(curBB);
                if(!curCaseVal)
                    continue;
                std::string caseNumStr = generateOperandStr(curCaseVal);
                switchStr+="\t case ";
                switchStr+=(caseNumStr+":");
                switchStr+=" goto ";
                switchStr+= curBB->getName();
                switchStr+=";\n";
            }
            switchStr += "\tdefault: goto ";
            switchStr += si.getDefaultDest()->getName();
            switchStr += ";\n}\n";
            bbContent->push_back(switchStr);
        }

        void generateTerminator()
        {
            if(isa<ReturnInst>(*insn))
                generateReturn();
            else if(isa<BranchInst>(*insn))
                generateBranch();
            else if(isa<SwitchInst>(*insn))
                generateSwitch();
            else
                llvm_unreachable("unsupported terminator type");
        }
        void generateBinaryOperator()
        {
            std::string binaryOpStr="";
            std::string varName = generateVariableName(insn);
            Value* firstOperand = insn->getOperand(0);
            Value* secondOperand = insn->getOperand(1);

            std::string firstOperandStr = generateOperandStr(firstOperand);
            std::string secondOperandStr = generateOperandStr(secondOperand);
            binaryOpStr += varName;
            binaryOpStr += " = ";
            // generate the actual computation
            switch(insn->getOpcode())
            {
                case Instruction::Add:
                case Instruction::FAdd:
                    binaryOpStr += firstOperandStr + "+";
                    break;
                case Instruction::Sub:
                case Instruction::FSub:
                    binaryOpStr += firstOperandStr + "-";
                    break;
                case Instruction::Mul:
                case Instruction::FMul:
                    binaryOpStr += firstOperandStr + "*";
                    break;
                case Instruction::UDiv:
                case Instruction::SDiv:
                case Instruction::FDiv:
                    binaryOpStr +=  firstOperandStr + "/";
                    break;
                case Instruction::URem:
                case Instruction::SRem:
                case Instruction::FRem:
                    binaryOpStr +=  firstOperandStr + "%";
                    break;
                case Instruction::Shl:
                    binaryOpStr +=  firstOperandStr + "<<";
                    break;
                case Instruction::LShr:
                case Instruction::AShr:
                    binaryOpStr +=  firstOperandStr + ">>";
                    break;
                case Instruction::And:
                    binaryOpStr +=  firstOperandStr + "&";
                    break;
                case Instruction::Or:
                    binaryOpStr +=  firstOperandStr + "|";
                    break;
                case Instruction::Xor:
                    binaryOpStr +=  firstOperandStr +"^";
                    break;
                default:
                    llvm_unreachable("unsupported binary operator");

            }
            binaryOpStr +=  secondOperandStr+";";
            bbContent->push_back(binaryOpStr);
        }
        void generateAllocaOperation()
        {
            std::string varName = generateVariableName(insn);

            Type* allocaPtrTy = insn->getType();
            assert(isa<PointerType>(*allocaPtrTy) &&"allocaInst not producing pointer type");
            PointerType& ptrType = cast<PointerType>(*allocaPtrTy);
            Type* pointedType = ptrType.getPointerElementType();
            std::string stackVarType = getLLVMTypeStr(pointedType);
            std::string stackVarName = varName+"_ele";
            std::string stackVarDec = stackVarType+" "+stackVarName+";";
            std::string ptrAssign = varName+ " = &"+stackVarName+";";
            topVarDecl->push_back(stackVarDec);
            topVarDecl->push_back(ptrAssign);

        }

        void generateLoadOperation()
        {
            std::string memoryOpStr="";
            std::string varName = generateVariableName(insn);
            LoadInst& li = cast<LoadInst>(*insn);
            Value* ldPtrVal = li.getPointerOperand();
            std::string ldPtrStr = generateOperandStr(ldPtrVal);

            bool pointerIsChannel = false;
            if(isa<Argument>(*ldPtrVal))
            {
                Argument* curFuncArg = &(cast<Argument>(*ldPtrVal));
                pointerIsChannel = isArgChannel(curFuncArg);
            }
            if(!getGeneratingCPU() || !pointerIsChannel)
            {
                memoryOpStr+=varName+"= *("+ldPtrStr+");";
            }
            else
            {
                memoryOpStr+="pop("+ldPtrStr+","+varName + ");";
            }
            bbContent->push_back(memoryOpStr);
        }

        void generateStoreOperation()
        {
            std::string memoryOpStr="";
            StoreInst& si = cast<StoreInst>(*insn);
            Value* stPtrVal = si.getPointerOperand();
            Value* stVal = si.getValueOperand();
            std::string stPtrStr = generateOperandStr(stPtrVal);
            std::string stValStr = generateOperandStr(stVal);
            // check if the pointer is an argument with
            // attribute channel
            bool pointerIsChannel = false;
            if(isa<Argument>(*stPtrVal))
            {
                Argument* curFuncArg = &(cast<Argument>(*stPtrVal));
                pointerIsChannel = isArgChannel(curFuncArg);
            }
            if(!getGeneratingCPU() || !pointerIsChannel )
            {
                memoryOpStr +="*("+stPtrStr+") = "+stValStr+";";
            }
            else
            {
                memoryOpStr +="push("+stPtrStr+","+ stValStr +");";
            }
            bbContent->push_back(memoryOpStr);
        }
        // I will need to add in the support for referencing members
        // in struct
        void generateGetElementOperation()
        {
            std::string memoryOpStr="";
            std::string varName = generateVariableName(insn);

            GetElementPtrInst& gepi = cast<GetElementPtrInst>(*insn);
            Value* ptr = gepi.getPointerOperand();
            std::string ptrStr = generateOperandStr( ptr);
            Value* offsetVal = gepi.getOperand(1);
            std::string offSetStr = generateOperandStr(offsetVal);
            // check the index array and do additions
            memoryOpStr += varName+"= "+ptrStr+"+"+offSetStr+";";
            bbContent->push_back(memoryOpStr);
        }
        void generateCastOperation()
        {
            CastInst& ci = cast<CastInst>(*insn);
            std::string castOpStr="";
            std::string varName = generateVariableName(insn);
            switch(insn->getOpcode())
            {
                case Instruction::Trunc:
                case Instruction::ZExt:
                case Instruction::SExt:
                case Instruction::BitCast:
                    // just generate a simple assignment(do we really need to cast? verilog would do it automatically)
                    castOpStr+= varName+" = "+generateOperandStr(ci.getOperand(0))+";";
                    break;
                default:
                    llvm_unreachable("unsupported cast operator");
            }
            bbContent->push_back(castOpStr);
        }
        void generateSelectOperation()
        {
            std::string selectOpStr="";
            std::string varName = generateVariableName(insn);
            SelectInst& si = cast<SelectInst>(*insn);
            Value* condVal =  si.getCondition();
            std::string condStr  = generateOperandStr(condVal);
            Value* trueVal =  si.getTrueValue();
            std::string trueStr = generateOperandStr(trueVal );
            Value* falseVal = si.getFalseValue();
            std::string falseStr = generateOperandStr(falseVal);
            selectOpStr += varName+ "="+condStr+"?"+trueStr+":"+falseStr+";";
            bbContent->push_back(selectOpStr);
        }
        void generateCmpOperation()
        {
            std::string cmpOpStr = "";
            std::string cmpOperator="";
            std::string varName = generateVariableName(insn);
                //int numberOfOperands = curIns.getNumOperands();
            CmpInst& ci = cast<CmpInst>(*insn);
            Value* first = ci.getOperand(0);
            Value* second = ci.getOperand(1);
            std::string firstVal = generateOperandStr(first);
            std::string secondVal = generateOperandStr(second);
            bool def=false;
            std::string constBool="";
            switch(ci.getPredicate())
            {
                case CmpInst::FCMP_FALSE:
                    def=true;
                    constBool+="0";
                    break;
                case CmpInst::FCMP_TRUE:
                    def=true;
                    constBool+="1";
                    break;
                case CmpInst::ICMP_EQ:
                case CmpInst::FCMP_OEQ:
                case CmpInst::FCMP_UEQ:
                    cmpOperator+="==";
                    break;
                case CmpInst::ICMP_SGT:
                case CmpInst::ICMP_UGT:
                case CmpInst::FCMP_UGT:
                case CmpInst::FCMP_OGT:
                    cmpOperator+=">";
                    break;
                case CmpInst::ICMP_SGE:
                case CmpInst::ICMP_UGE:
                case CmpInst::FCMP_UGE:
                case CmpInst::FCMP_OGE:
                    cmpOperator+=">=";
                    break;
                case CmpInst::ICMP_SLT:
                case CmpInst::ICMP_ULT:
                case CmpInst::FCMP_ULT:
                case CmpInst::FCMP_OLT:
                    cmpOperator+="<";
                    break;
                case CmpInst::ICMP_ULE:
                case CmpInst::ICMP_SLE:
                case CmpInst::FCMP_ULE:
                case CmpInst::FCMP_OLE:
                    cmpOperator+="<=";
                    break;
                case CmpInst::ICMP_NE:
                case CmpInst::FCMP_UNE:
                case CmpInst::FCMP_ONE:
                    cmpOperator+="!=";
                    break;
                default:
                    llvm_unreachable("unsupported cmp operation");
            }
            if(def)
                cmpOpStr+= varName+"="+constBool+";";
            else
                cmpOpStr+= varName+"="+firstVal+cmpOperator+secondVal+";";
            bbContent->push_back(cmpOpStr);

        }
        void generateCallOperation()
        {
            CallInst& ci = cast<CallInst>(*insn);
            Function* callee = ci.getCalledFunction();
            std::string varName = generateVariableName(insn);
            std::string callOpStr = "";
            if(!callee->getReturnType()->isVoidTy())
            {
                callOpStr+=varName;
                callOpStr+="=";
            }

            if(getGeneratingCPU())
            {
                callOpStr+= callee->getName();
                callOpStr+="(";
                for(unsigned i = 0; i<ci.getNumArgOperands(); i++)
                {
                    Value* curOp = ci.getArgOperand(i);
                    callOpStr+=generateOperandStr(curOp);
                    if(i!=ci.getNumArgOperands()-1)
                        callOpStr+=",";
                }
                callOpStr+=");";
            }
            else
            {
                callOpStr+=generateDeviceName(callee);
                callOpStr+="_Get_Return(&";
                callOpStr+= generateDeviceVarName(callee)+");\n";
            }
            bbContent->push_back(callOpStr);
        }
        void generateInstructionBody()
        {
            if(isa<TerminatorInst>(*insn))
                generateTerminator();
            else if(isa<PHINode>(*insn))
                generatePhiNode();
            else if(isa<BinaryOperator>(*insn))
                generateBinaryOperator();
            else if(isa<AllocaInst>(*insn))
                generateAllocaOperation();
            else if(isa<LoadInst>(*insn))
                generateLoadOperation();
            else if(isa<StoreInst>(*insn))
                generateStoreOperation();
            else if(isa<GetElementPtrInst>(*insn))
                generateGetElementOperation();
            else if(isa<CastInst>(*insn))
                generateCastOperation();
            else if(isa<SelectInst>(*insn))
                generateSelectOperation();
            else if(isa<CmpInst>(*insn))
                generateCmpOperation();
            else if(isa<CallInst>(*insn))
                generateCallOperation();
            else
                llvm_unreachable("unsupported operation");

        }

/*
        void generateMemoryOperation()
        {
            std::string memoryOpStr="";
            std::string varName = generateVariableName(insn);
            if(isa<AllocaInst>(*insn))
            {
                Type* allocaPtrTy = insn->getType();
                assert(isa<PointerType>(*allocaPtrTy) &&"allocaInst not producing pointer type");
                PointerType& ptrType = cast<PointerType>(*allocaPtrTy);
                Type* pointedType = ptrType.getPointerElementType();
                std::string stackVarType = getLLVMTypeStr(pointedType);
                std::string stackVarName = varName+"_ele";
                std::string stackVarDec = stackVarType+" "+stackVarName+";";
                std::string ptrAssign = varName+ " = &"+stackVarDec+";";
                topVarDecl->push_back(stackVarDec);
                topVarDecl->push_back(ptrAssign);
            }
            else if(isa<LoadInst>(*insn))
            {
                LoadInst& li = cast<LoadInst>(*insn);
                Value* ldPtrVal = li.getPointerOperand();
                std::string ldPtrStr = generateOperandStr(ldPtrVal);
                memoryOpStr+=varName+"= *("+ldPtrStr+");";
            }
            else if(isa<StoreInst>(*insn))
            {
                StoreInst& si = cast<StoreInst>(*insn);
                Value* stPtrVal = si.getPointerOperand();
                Value* stVal = si.getValueOperand();
                std::string stPtrStr = generateOperandStr(stPtrVal);
                std::string stValStr = generateOperandStr(stVal);
                memoryOpStr +="*("+stPtrStr+") = "+stValStr+";";
            }
            else if(isa<GetElementPtrInst>(*insn))
            {
                GetElementPtrInst& gepi = cast<GetElementPtrInst>(*insn);
                Value* ptr = gepi.getPointerOperand();
                std::string ptrStr = generateOperandStr( ptr);
                Value* offsetVal = gepi.getOperand(1);
                std::string offSetStr = generateOperandStr(offsetVal);
                // check the index array and do additions
                memoryOpStr += varName+"= "+ptrStr+"+"+offSetStr+";";

            }
            else
                llvm_unreachable("unsupported memory operation");
            bbContent->push_back(memoryOpStr);
        }
*/
    public:
        InstructionGenerator(Instruction* curIns,
                             std::vector<std::string>* varDecl,
                             std::vector<std::string>* curBBContentStr,
                             std::map<BasicBlock*, std::vector<std::string>*>* phiAssign)
        {
            insn=curIns;
            topVarDecl = varDecl;
            bbContent = curBBContentStr;
            phiPreAssign = phiAssign;
        }


        void generateInstruction()
        {
            std::string curVarDec = generateVarDecl();
            if(curVarDec!="")
                topVarDecl->push_back(curVarDec);
            generateInstructionBody();

        }
    };
}

#endif // GENCINSN_H
