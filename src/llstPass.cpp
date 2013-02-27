#define DEBUG_TYPE "llst"
#include <llstPass.h>

STATISTIC(rootLoadsRemoved, "Number of removed loads from gc.root protected pointers                                <<<<<<");

using namespace llvm;

namespace {
    struct LLSTPass : public FunctionPass {
    private:
        std::set<std::string> GCFunctionNames;
        bool isPotentialGCFunction(std::string name);
        bool removeRootLoads(BasicBlock* B);
    
    public:
        virtual bool runOnFunction(Function &F) ;
        static char ID;
        LLSTPass(): FunctionPass(ID) {
            GCFunctionNames.insert("newOrdinaryObject");
            GCFunctionNames.insert("newBinaryObject");
            GCFunctionNames.insert("sendMessage");
            GCFunctionNames.insert("invokeBlock");
            GCFunctionNames.insert("createBlock");
        }
    };
}

char LLSTPass::ID = 0;
static RegisterPass<LLSTPass> LLSTSpecificPass("llst", "LLST language specific pass", false /* Only looks at CFG */, false /* Analysis Pass */);

FunctionPass* createLLSTPass() {
  return new LLSTPass();
}

bool LLSTPass::runOnFunction(Function& F)
{
    bool CFGChanged = false;
    for (Function::iterator B = F.begin(); B != F.end(); ++B) {
        CFGChanged |= removeRootLoads(B);
    }
    
    return CFGChanged;
}

bool isLoadFromRoot(Instruction* Instr) {
    return true;
    ///TODO ensure Instr.getOperand(0) points to root
    
}

bool LLSTPass::isPotentialGCFunction(std::string name)
{
    return GCFunctionNames.find(name) != GCFunctionNames.end();
}


bool LLSTPass::removeRootLoads(BasicBlock* B)
{
    typedef std::vector<Instruction*>   InstructionVector;
    typedef std::set<Instruction*>      InstructionSet;
    typedef InstructionSet::iterator    InstructionSI;
    
    InstructionVector LoadNCall;
    InstructionSet toDelete;
    
    for (BasicBlock::iterator Instr = B->begin(); Instr != B->end(); Instr++ )
    {
        if ( LoadInst* load = dyn_cast<LoadInst>(Instr) ) {
            if (isLoadFromRoot(load))
                LoadNCall.push_back(load); 
        }
        if ( CallInst* call = dyn_cast<CallInst>(Instr) ) {
            //Is it a call that might collect garbage?
            std::string name = call->getCalledFunction()->getName();
            if ( isPotentialGCFunction(name) ) {
                LoadNCall.push_back(call);
            }
        }
    }
    
    for(size_t i = 0; i < LoadNCall.size(); i++)
    {
        if( isa<CallInst>(LoadNCall[i]) )
            continue;
        
        LoadInst* CurrentLoad = dyn_cast<LoadInst>(LoadNCall[i]);
        
        for(size_t j = i+1; j < LoadNCall.size(); j++)
        {
            if( isa<CallInst>(LoadNCall[j]) )
                break; // we have faced a call that might collect garbage
            
            LoadInst* NextLoad = dyn_cast<LoadInst>(LoadNCall[j]);
            
            if (NextLoad->isIdenticalTo(CurrentLoad)) {
                //Loads are equal
                NextLoad->replaceAllUsesWith( CurrentLoad );
                toDelete.insert( NextLoad ); //remove usless loads later
            }
        }
    }
    
    rootLoadsRemoved += toDelete.size();
    
    bool BBChanged = !toDelete.empty();
    for(InstructionSI I = toDelete.begin(); I != toDelete.end(); I++) {
        (*I)->eraseFromParent();
    }
    
    return BBChanged;
}

