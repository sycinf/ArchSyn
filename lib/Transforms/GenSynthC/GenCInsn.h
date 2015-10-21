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
                switchStr+="\t";
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
            // doing something special for phi as it does not involve actual
            // computation
            if(isa<TerminatorInst>(*insn))
                generateTerminator();
            if(isa<PHINode>(*insn))
                generatePhiNode();

        }
    };
}

#endif // GENCINSN_H
