#ifndef LLVM_ANALYSIS_INSTRUCTIONGRAPH_H
#define LLVM_ANALYSIS_INSTRUCTIONGRAPH_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/IR/CallSite.h"
//#include "llvm/Support/IncludeFile.h"
#include "llvm/IR/ValueHandle.h"
#include <map>

namespace llvm {

class Function;
class Module;
class InstructionGraphNode;

typedef std::map<BasicBlock*, std::vector<BasicBlock*>*> BB2BBVectorMapTy;


//===----------------------------------------------------------------------===//
// CallGraph class definition
//
class InstructionGraph : public FunctionPass {
  Function *Func;              // The function this instruction graph represents

  typedef std::map<const Instruction *, InstructionGraphNode *> InstructionMapTy;
  InstructionMapTy InstructionMap;    // Map from a instruction to its node

  // Root is root of the ins graph, should be the external node with children
  // being every instruction in our graph
  InstructionGraphNode *Root;

  // ExternalInsNode - This node has edges to all instruction
  InstructionGraphNode *ExternalInsNode;

  // CallsExternalNode - This node has edges to it from all functions making
  // indirect calls or calling an external function.
  //CallGraphNode *CallsExternalNode;

  /// Replace the function represented by this node by another.
  /// This does not rescan the body of the function, so it is suitable when
  /// splicing the body of one function to another while also updating all
  /// callers from the old function to the new.
  ///
  /* Shaoyi: we probably wont add instruction
  //void spliceFunction(const Function *From, const Function *To);
  */

  void addToInstructionGraph(Instruction *I, std::vector<BasicBlock*>* earliestPred);
  //BB2BBVectorMapTy BasicBlockMap2PredAcceptors;



  // more aggressive predicating --- if BasicBlock A is a post dominator
  // of BasicBlock B, then thr control dependence should be from ancestors
  // of B which is not post dominated by A
  // first we need to construct the bb to predecessor map
  BB2BBVectorMapTy BasicBlock2Predecessors;



public:
  static char ID; // Class identification, replacement for typeinfo
  //===---------------------------------------------------------------------
  // Accessors.
  //
  typedef InstructionMapTy::iterator iterator;
  typedef InstructionMapTy::const_iterator const_iterator;

  /// getModule - Return the module the call graph corresponds to.
  ///
  Function &getFunction() const { return *Func; }

  inline       iterator begin()       { return InstructionMap.begin(); }
  inline       iterator end()         { return InstructionMap.end();   }
  inline const_iterator begin() const { return InstructionMap.begin(); }
  inline const_iterator end()   const { return InstructionMap.end();   }

  // Subscripting operators, return the call graph node for the provided
  // function
  inline const InstructionGraphNode *operator[](const Instruction *F) const {
    const_iterator I = InstructionMap.find(F);
    assert(I != InstructionMap.end() && "Instruction not in insgraph!");
    return I->second;
  }
  inline InstructionGraphNode *operator[](const Instruction *F) {
    const_iterator I = InstructionMap.find(F);
    assert(I != InstructionMap.end() && "Instruction not in insgraph!");
    return I->second;
  }

  /// Returns the CallGraphNode which is used to represent undetermined calls
  /// into the callgraph.
  InstructionGraphNode *getExternalInsNode() const { return ExternalInsNode; }
  //CallGraphNode *getCallsExternalNode() const { return CallsExternalNode; }

  /// Return the root/main method in the module, or some other root node, such
  /// as the externalcallingnode.
  InstructionGraphNode *getRoot() { return Root; }
  const InstructionGraphNode *getRoot() const { return Root; }


  BB2BBVectorMapTy* getPredecessorMap()
  {
      return &BasicBlock2Predecessors;
  }

  //===---------------------------------------------------------------------
  // Functions to keep a call graph up to date with a function that has been
  // modified.
  //

  /// removeFunctionFromModule - Unlink the function from this module, returning
  /// it.  Because this removes the function from the module, the call graph
  /// node is destroyed.  This is only valid if the function does not call any
  /// other functions (ie, there are no edges in it's CGN).  The easiest way to
  /// do this is to dropAllReferences before calling this.
  ///
  //Function *removeFunctionFromModule(CallGraphNode *CGN);

  /// getOrInsertFunction - This method is identical to calling operator[], but
  /// it will insert a new CallGraphNode for the specified function if one does
  /// not already exist.
  InstructionGraphNode *getOrInsertInstruction(const Instruction *F);

  InstructionGraph();
  virtual ~InstructionGraph() { releaseMemory(); }
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnFunction(Function &F);
  virtual void releaseMemory();

  void print(raw_ostream &o, const Function* = 0) const;
  void dump() const;
};

//===----------------------------------------------------------------------===//
// InstructionGraphNode class definition.
//
class InstructionGraphNode {
  friend class InstructionGraph;

  AssertingVH<Instruction> I;

  // CallRecord - This is a pair of the calling instruction (a call or invoke)
  // and the callgraph node being called.
public:
  typedef std::pair<WeakVH, InstructionGraphNode*> InstructionRecord;
private:
  std::vector<InstructionRecord> DependentInstructions;

  /// NumReferences - This is the number of times that this CallGraphNode occurs
  /// in the CalledFunctions array of this or other CallGraphNodes.
  //unsigned NumReferences;

  //InstructionGraphNode(const InstructionGraphNode &) LLVM_DELETED_FUNCTION;
  //void operator=(const CallGraphNode &) LLVM_DELETED_FUNCTION;

  //void DropRef() { --NumReferences; }
  //void AddRef() { ++NumReferences; }
public:
  typedef std::vector<InstructionRecord> DependentInstructionsVector;


  // CallGraphNode ctor - Create a node for the specified function.
  inline InstructionGraphNode(Instruction *f) : I(f) {}
  ~InstructionGraphNode() { }

  //===---------------------------------------------------------------------
  // Accessor methods.
  //

  typedef std::vector<InstructionRecord>::iterator iterator;
  typedef std::vector<InstructionRecord>::const_iterator const_iterator;

  // getInstruction - Return the instruction that this ins graph node represents.
  Instruction *getInstruction() const { return I; }

  inline iterator begin() { return DependentInstructions.begin(); }
  inline iterator end()   { return DependentInstructions.end();   }
  inline const_iterator begin() const { return DependentInstructions.begin(); }
  inline const_iterator end()   const { return DependentInstructions.end();   }
  inline bool empty() const { return DependentInstructions.empty(); }
  inline unsigned size() const { return (unsigned)DependentInstructions.size(); }

  /// getNumReferences - Return the number of other CallGraphNodes in this
  /// CallGraph that reference this node in their callee list.
  //unsigned getNumReferences() const { return NumReferences; }

  // Subscripting operator - Return the i'th called function.
  //
  InstructionGraphNode *operator[](unsigned i) const {
    assert(i < DependentInstructions.size() && "Invalid index");
    return DependentInstructions[i].second;
  }

  /// dump - Print out this call graph node.
  ///
  void dump() const;
  void print(raw_ostream &OS) const;

  //===---------------------------------------------------------------------
  // Methods to keep a call graph up to date with a function that has been
  // modified
  //

  /// removeAllDependentInstructions - As the name implies, this removes all edges
  /// from this InstructionGraphNode to any instructions dependent on it.
  /*void removeAllCalledFunctions() {
    while (!CalledFunctions.empty()) {
      CalledFunctions.back().second->DropRef();
      CalledFunctions.pop_back();
    }
  }*/

  /// stealCalledFunctionsFrom - Move all the callee information from N to this
  /// node.
  /*void stealCalledFunctionsFrom(CallGraphNode *N) {
    assert(CalledFunctions.empty() &&
           "Cannot steal callsite information if I already have some");
    std::swap(CalledFunctions, N->CalledFunctions);
  }*/


  /// addDependentInstruction - Add an instruction to the list of instructions depending on this
  /// one.no need to worry about the pseudo external instruction coz nobody gonna call this function
  /// on it
  void addDependentInstruction( InstructionGraphNode *M) {
    DependentInstructions.push_back(std::make_pair(M->getInstruction(), M));

  }

  /*void removeCallEdge(iterator I) {
    I->second->DropRef();
    *I = CalledFunctions.back();
    CalledFunctions.pop_back();
  }*/

/*
  /// removeCallEdgeFor - This method removes the edge in the node for the
  /// specified call site.  Note that this method takes linear time, so it
  /// should be used sparingly.
  void removeCallEdgeFor(CallSite CS);
  /// removeAnyCallEdgeTo - This method removes all call edges from this node
  /// to the specified callee function.  This takes more time to execute than
  /// removeCallEdgeTo, so it should not be used unless necessary.
  void removeAnyCallEdgeTo(CallGraphNode *Callee);
  /// removeOneAbstractEdgeTo - Remove one edge associated with a null callsite
  /// from this node to the specified callee function.
  void removeOneAbstractEdgeTo(CallGraphNode *Callee);
  /// replaceCallEdge - This method replaces the edge in the node for the
  /// specified call site with a new one.  Note that this method takes linear
  /// time, so it should be used sparingly.
  void replaceCallEdge(CallSite CS, CallSite NewCS, CallGraphNode *NewNode);
  /// allReferencesDropped - This is a special function that should only be
  /// used by the CallGraph class.
  void allReferencesDropped() {
    NumReferences = 0;
  }*/
};

//===----------------------------------------------------------------------===//
// GraphTraits specializations for call graphs so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for tranversing call graphs using standard graph
// traversals.
template <> struct GraphTraits<InstructionGraphNode*> {
  typedef InstructionGraphNode NodeType;

  typedef InstructionGraphNode::InstructionRecord IGNPairTy;
  typedef std::pointer_to_unary_function<IGNPairTy, InstructionGraphNode*> IGNDerefFun;

  static NodeType *getEntryNode(InstructionGraphNode *IGN) { return IGN; }

  typedef mapped_iterator<NodeType::iterator, IGNDerefFun> ChildIteratorType;

  static inline ChildIteratorType child_begin(NodeType *N) {
    return map_iterator(N->begin(), IGNDerefFun(IGNDeref));
  }
  static inline ChildIteratorType child_end  (NodeType *N) {
    return map_iterator(N->end(), IGNDerefFun(IGNDeref));
  }

  static InstructionGraphNode *IGNDeref(IGNPairTy P) {
    return P.second;
  }

};

template <> struct GraphTraits<const InstructionGraphNode*> {
  typedef const InstructionGraphNode NodeType;
  typedef NodeType::const_iterator ChildIteratorType;

  static NodeType *getEntryNode(const InstructionGraphNode *IGN) { return IGN; }
  static inline ChildIteratorType child_begin(NodeType *N) { return N->begin();}
  static inline ChildIteratorType child_end  (NodeType *N) { return N->end(); }
};

template<> struct GraphTraits<InstructionGraph*> : public GraphTraits<InstructionGraphNode*> {
  static NodeType *getEntryNode(InstructionGraph *IGN) {
    return IGN->getExternalInsNode();  // Start at the external node!
  }
  typedef std::pair<const Instruction*, InstructionGraphNode*> PairTy;
  typedef std::pointer_to_unary_function<PairTy, InstructionGraphNode&> DerefFun;

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<InstructionGraph::iterator, DerefFun> nodes_iterator;
  static nodes_iterator nodes_begin(InstructionGraph *IG) {
    return map_iterator(IG->begin(), DerefFun(IGdereference));
  }
  static nodes_iterator nodes_end  (InstructionGraph *IG) {
    return map_iterator(IG->end(), DerefFun(IGdereference));
  }

  static InstructionGraphNode &IGdereference(PairTy P) {
    return *P.second;
  }
};

template<> struct GraphTraits<const InstructionGraph*> :
  public GraphTraits<const InstructionGraphNode*> {
  static NodeType *getEntryNode(const InstructionGraph *IGN) {
    return IGN->getExternalInsNode();
  }
  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef InstructionGraph::const_iterator nodes_iterator;
  static nodes_iterator nodes_begin(const InstructionGraph *IG) { return IG->begin(); }
  static nodes_iterator nodes_end  (const InstructionGraph *IG) { return IG->end(); }
};

} // End llvm namespace

// Make sure that any clients of this file link in CallGraph.cpp
//FORCE_DEFINING_FILE_TO_BE_LINKED(InstructionGraph)

#endif
