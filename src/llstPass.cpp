#include <llstPass.h>
#include <jit.h>

namespace {
    struct LLSTPass : public FunctionPass {
        void getAnalysisUsage(AnalysisUsage &AU) const {
            FunctionPass::getAnalysisUsage(AU);
        };
        virtual bool runOnFunction(Function &F) ;
        static char ID;
        LLSTPass(): FunctionPass(ID) { }
    private:
        
        bool removeRootLoads(BasicBlock* B);
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

bool LLSTPass::removeRootLoads(BasicBlock* B)
{
    typedef std::vector<Instruction*>   InstructionVector;
    typedef std::set<Instruction*>      InstructionSet;
    typedef InstructionSet::iterator    InstructionSI;
    
    typedef std::map<Instruction*, Instruction*>    InstructionMap;
    typedef InstructionMap::iterator                InstructionMI;
    
    
    InstructionVector LoadNCall;
    InstructionSet toDelete;
    
    for (BasicBlock::iterator Instr = B->begin(); Instr != B->end(); Instr++ ) {
        if ( isa<LoadInst>(Instr) )
            LoadNCall.push_back(&*Instr); //TODO ensure Instr.getOperand(0) points to root

        if ( isa<CallInst>(Instr) ) {
            //Is it a call that might collect garbage?
            CallInst* call = dyn_cast<CallInst>(Instr);
            std::string name = call->getCalledFunction()->getName();
            if (   name == "newOrdinaryObject"
                || name == "newBinaryObject"
                || name == "sendMessage"
                || name == "invokeBlock"
                || name == "createBlock"
            ) {
                LoadNCall.push_back(&*Instr);
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
                break; // we face a call that might collect garbage
            
            LoadInst* NextLoad = dyn_cast<LoadInst>(LoadNCall[j]);
            
            if ( NextLoad->getPointerOperand() != CurrentLoad->getPointerOperand() ) {
                // Loads do not point to the same pointer
                continue;
            }
            
            NextLoad->replaceAllUsesWith( CurrentLoad );
            toDelete.insert( NextLoad ); //remove usless loads later
            continue;

        }
    }
    
    bool BBChanged = !toDelete.empty();
    for(InstructionSI I = toDelete.begin(); I != toDelete.end(); I++) {
        (*I)->eraseFromParent();
    }

    return BBChanged;
}

