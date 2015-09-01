#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/InstructionGraph.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/DecoupleInsScc/DecoupleInsScc.h"
#include <algorithm>
#include <memory>
using namespace llvm;

// command line options...
static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static cl::opt<std::string>
OutputFIFOFilename("fdes", cl::desc("FIFO connection description"),
               cl::value_desc("filename"));


static cl::opt<bool>
NoControlFlowDup("disable-cf-dup",
                 cl::desc("Do not duplicate control flows between different stages"));

// the decoupling will be the same
// but the back end for generating C file will be a bit different
// as the CPU one will generate software queue and conditional variables
// Note:: if streaming memory access is turned on, cpu will also have those
// functions emulating streaming memory access by doing memcpy
static cl::opt<bool>
GenerateCPUMode("cpu-mode",
                  cl::desc("generate the decoupled functions which would be run by the cpu"));

static cl::opt<bool>
NoOutput("disable-output",
         cl::desc("Do not write result bitcode file"), cl::Hidden);









//===----------------------------------------------------------------------===//
// main for dppgen
//
int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  LLVMContext &Context = getGlobalContext();

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeInstructionGraphPass(Registry);
  INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(PostDominatorTree)
  INITIALIZE_PASS_DEPENDENCY(LoopInfo)

  cl::ParseCommandLineOptions(argc, argv,
    "llvm .bc -> .cpp generate decoupled processing pipeline\n");


  SMDiagnostic Err;

  // Load the input module...
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  //M.reset(ParseIRFile(InputFilename, Err, Context));

  if (M.get() == 0) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<tool_output_file> Out;
  if (NoOutput) {
    if (!OutputFilename.empty())
      errs() << "WARNING: The -o (output filename) option is ignored when\n"
                "the --disable-output option is used.\n";
  } else {
    // Default to standard output.
    if (OutputFilename.empty())
      OutputFilename = "-";

    std::error_code EC;
    Out.reset(new tool_output_file(OutputFilename, EC, sys::fs::F_None));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }
  }



  // a separate stream
  std::unique_ptr<tool_output_file>  fdesOut;

  // do the samething for FIFO description
  if (NoOutput) {
    if (!OutputFIFOFilename.empty())
      errs() << "WARNING: The -fdes (fifo des filename) option is ignored when\n"
                "the --disable-output option is used.\n";
  } else {
    // Default to standard output.
    if (OutputFIFOFilename.empty())
      OutputFilename = "-";

    std::error_code EC;
    fdesOut.reset(new tool_output_file(OutputFilename, EC, sys::fs::F_None));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }
  }




  PassManager Passes;
  // we will need two passes
  // the first pass is to generate multiple function from a single function
  // the second pass is to generate the synthesizable C version for each generated function


  Passes.add(llvm::createDecoupleInsSccPass(!NoControlFlowDup));

  Passes.add(createPrintModulePass(Out->os()));


  //PartitionGen* pg = new PartitionGen(Out->os(),fdesOut->os(),NoControlFlowDup,GenerateCPUMode);

  //Passes.add(pg );
  // Create a new optimization pass for each one specified on the command line
  /*for (unsigned i = 0; i < PassList.size(); ++i) {
    // Check to see if -std-compile-opts was specified before this option.  If
    // so, handle it.
    const PassInfo *PassInf = PassList[i];
    Pass *P = 0;
    if (PassInf->getNormalCtor())
      P = PassInf->getNormalCtor()();
    else
      errs() << argv[0] << ": cannot create pass: "
             << PassInf->getPassName() << "\n";
    if (P) {
      PassKind Kind = P->getPassKind();
      addPass(Passes, P);
    }
  }*/

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  // Now that we have all of the passes ready, run them.
  Passes.run(*M.get());




  // Declare success.
  if (!NoOutput )
  {
    Out->keep();
    fdesOut->keep();
  }
  return 0;
}
