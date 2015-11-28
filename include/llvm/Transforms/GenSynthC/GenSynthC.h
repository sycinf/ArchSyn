#ifndef GENSYNTHC_H
#define GENSYNTHC_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
#define HLSFPGA_CLKPERIOD "8"
#define HLSFPGA_CLKUNCERTAIN "0.5"
// whole bunch of zynq zedboard specific things
#define HLSFPGA "xc7z020clg484-1"
#define HLSZEDBOARD "em.avnet.com:zed:part0:1.3"
#define HLSPSIPName "xilinx.com:ip:processing_system7"
#define HLSPSIPVersion "5.5"
#define HLSPSIPInstName "processing_sys7"
#define HLSBDRULE "xilinx.com:bd_rule:processing_system7 -config {make_external \"FIXED_IO, DDR\" apply_board_preset \"1\" Master \"Disable\" Slave \"Disable\" }"
#define HLSIPPREFIX "xilinx.com:hls:"
#define XILINXIPPREFIX "xilinx.com:ip:"
#define XILINXINTCONVERSION "2.1"

namespace llvm{
    ModulePass* createGenSynthCPass(llvm::raw_ostream &OS,bool targetCPU);
}



#endif // GENSYNTHC_H
