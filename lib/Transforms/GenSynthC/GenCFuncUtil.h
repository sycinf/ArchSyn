#ifndef GENCFUNCUTIL_H
#define GENCFUNCUTIL_H
#include <string>
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Transforms/GenSynthC/GenSynthC.h"
#include "llvm/IR/Constants.h"
#include <boost/lexical_cast.hpp>
using namespace llvm;
static int numTabs =0;

static bool CPU_bar_HLS;
// for the following lines,add/reduce tabs
static void addBarSubTabs(bool addBarSub)
{
    if(addBarSub)
        numTabs++;
    else
        numTabs--;
}

static void printTabbedLines(raw_ostream& out, std::string lineStr)
{
    std::stringstream multiLine(lineStr);
    std::string curLine;
    while(std::getline(multiLine,curLine))
    {
        for(unsigned tabCount = 0; tabCount<numTabs; tabCount++)
        {
            out<<"\t";
        }
        out<<curLine<<"\n";
    }

}

static int getInstructionSeqNum(Instruction* ins)
{
    BasicBlock* BB=ins->getParent();
    int seqNum = -1;
    for(BasicBlock::iterator insPt = BB->begin(), insEnd = BB->end(); insPt != insEnd; insPt++)
    {
        seqNum++;
        if( ins == insPt)
            break;
    }
    return seqNum;
}

static std::string generateConstantStr(Constant& original)
{
    assert((isa<ConstantFP>(original) || isa<ConstantInt>(original)) &&
           "unsupported constant format");
    std::string rtStr = "";
    if(isa<ConstantFP>(original))
    {
        ConstantFP& fpRef = cast<ConstantFP>(original);
        APFloat pf = fpRef.getValueAPF();
        SmallVector<char, 32> Buffer;
        pf.toString(Buffer, 10,10);
        for(SmallVector<char, 32>::iterator cur=Buffer.begin(), end=Buffer.end();cur!=end; cur++ )
            rtStr.append(1,*cur);
    }
    else if(isa<ConstantInt>(original))
    {
        ConstantInt& intRef = cast<ConstantInt>(original);
        APInt pint = intRef.getValue();
        std::string str=pint.toString(10,true);
        rtStr = rtStr +str;
    }
    return rtStr;
}

static std::string generateVariableName(Instruction* ins)
{
    int seqNum = getInstructionSeqNum(ins);
    std::string rtVarName= ins->getParent()->getName();
    rtVarName = rtVarName+boost::lexical_cast<std::string>(seqNum);
    return rtVarName;
}




std::string getLLVMTypeStr(Type *Ty) {
  switch (Ty->getTypeID()) {
      case Type::VoidTyID:      return "void";
      case Type::HalfTyID:      return "half";
      case Type::FloatTyID:     return "float";
      case Type::DoubleTyID:    return "double";
      case Type::X86_FP80TyID:  return "x86_fp80";
      case Type::FP128TyID:     return "fp128";
      case Type::PPC_FP128TyID: return "ppc_fp128";
      case Type::LabelTyID:     return "label";
      case Type::MetadataTyID:  return "metadata";
      case Type::X86_MMXTyID:   return "x86_mmx";
      case Type::IntegerTyID: {
         std::string curType = "ap_int<" + boost::lexical_cast<std::string>(cast<IntegerType>(Ty)->getBitWidth())+">";
         return curType;
      }
      case Type::FunctionTyID: {
          assert(false && "not generating function ID");
          /*
        FunctionType *FTy = cast<FunctionType>(Ty);
        print(FTy->getReturnType(), OS);
        OS << " (";
        for (FunctionType::param_iterator I = FTy->param_begin(),
             E = FTy->param_end(); I != E; ++I) {
          if (I != FTy->param_begin())
            OS << ", ";
          print(*I, OS);
        }
        if (FTy->isVarArg()) {
          if (FTy->getNumParams()) OS << ", ";
          OS << "...";
        }
        OS << ')';*/

      }
      case Type::StructTyID: {
        StructType *STy = cast<StructType>(Ty);
        std::string curStructType = STy->getName();
        return curStructType;

      }
      case Type::PointerTyID: {
        Type* ptedType = Ty->getPointerElementType();
        std::string ptedTypeStr = getLLVMTypeStr(ptedType);
        std::string ptrType = ptedTypeStr+"*";
        return ptrType;
      }
      case Type::ArrayTyID: {
        ArrayType *ATy = cast<ArrayType>(Ty);
        Type* arrElementType = ATy->getElementType();
        std::string elementTypeStr = getLLVMTypeStr(arrElementType);
        std::string arrayTypeStr = elementTypeStr+"["+boost::lexical_cast<std::string>(ATy->getNumElements())+"]";
        return arrayTypeStr;
      }
      case Type::VectorTyID: {
        assert(false && "currently not supporting vector types yet");
        /*VectorType *PTy = cast<VectorType>(Ty);
        OS << "<" << PTy->getNumElements() << " x ";
        print(PTy->getElementType(), OS);
        OS << '>';
        return;*/
      }
  }
  llvm_unreachable("Invalid TypeID");
}


#endif // GENCFUNCUTIL_H
