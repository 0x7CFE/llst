#define DEBUG_TYPE "llst"
#include <llstPass.h>

STATISTIC(rootLoadsRemoved, "Number of removed loads from gc.root protected pointers                       <<<<<<");
STATISTIC(rootsRemoved,     "Number of removed roots                                                       <<<<<<");

using namespace llvm;

typedef std::vector<Instruction*>   InstructionVector;
typedef std::set<Instruction*>      InstructionSet;
typedef InstructionSet::iterator    InstructionSI;

namespace {
    struct LLSTPass : public FunctionPass {
    private:
        std::set<std::string> m_GCFunctionNames;
        bool isPotentialGCFunction(Function* F);
        bool removeRootLoads(BasicBlock* B); // TODO We should implement Analysis pass ( ::canInstructionRangeModify() ). The standart GVN pass will do the rest for us.
        bool removeRedundantRoots(Function& F);
        int  getNumRoots(Function& F);
    public:
        virtual bool runOnFunction(Function &F) ;
        static char ID;
        LLSTPass(): FunctionPass(ID) {
            m_GCFunctionNames.insert("newOrdinaryObject");
            m_GCFunctionNames.insert("newBinaryObject");
            m_GCFunctionNames.insert("sendMessage");
            m_GCFunctionNames.insert("invokeBlock");
            m_GCFunctionNames.insert("createBlock");
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
    CFGChanged |= removeRedundantRoots(F);
    return CFGChanged;
}

bool isLoadFromRoot(LoadInst* Load) {
    AllocaInst* pointerAlloc = dyn_cast<AllocaInst>(Load->getPointerOperand());
    if (!pointerAlloc)
        return false;
    
    Function* F = Load->getParent()->getParent();
    BasicBlock& entryBB = F->getEntryBlock();
    
    for (BasicBlock::iterator I = entryBB.begin(); I != entryBB.end(); ++I) {
        if (IntrinsicInst* createRootCall = dyn_cast<IntrinsicInst>(I)) {
            if(createRootCall->getCalledFunction()->getIntrinsicID() == Intrinsic::gcroot) {
                Value* holder = createRootCall->getArgOperand(0)->stripPointerCasts();
                if (pointerAlloc->isIdenticalTo(cast<AllocaInst>(holder)) && pointerAlloc->getName() == holder->getName()) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool LLSTPass::isPotentialGCFunction(Function* F)
{
    std::string name = F->getName();
    if (m_GCFunctionNames.find(name) != m_GCFunctionNames.end())
        return true;
    
    if (F->isIntrinsic())
        return false;
    
    if ( F->getGC() )
        return true;
    return false;
}


bool LLSTPass::removeRootLoads(BasicBlock* B)
{
    InstructionVector LoadNCall;
    InstructionSet toDelete;
    
    for (BasicBlock::iterator Instr = B->begin(); Instr != B->end(); ++Instr)
    {
        if ( LoadInst* load = dyn_cast<LoadInst>(Instr) ) {
            if (isLoadFromRoot(load))
                LoadNCall.push_back(load);
        }
        if ( CallInst* call = dyn_cast<CallInst>(Instr) ) {
            //Is it a call that might collect garbage?
            if ( isPotentialGCFunction(call->getCalledFunction()) ) {
                LoadNCall.push_back(call);
            }
        }
    }
    
    for(std::size_t i = 0; i < LoadNCall.size(); i++)
    {
        if( isa<CallInst>(LoadNCall[i]) )
            continue;
        
        LoadInst* CurrentLoad = dyn_cast<LoadInst>(LoadNCall[i]);
        
        for(std::size_t j = i+1; j < LoadNCall.size(); j++)
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
    for(InstructionSI I = toDelete.begin(); I != toDelete.end(); ++I) {
        (*I)->eraseFromParent();
    }
    
    return BBChanged;
}

int LLSTPass::getNumRoots(Function& F) {
    int result = 0;
    for (Function::iterator BB = F.begin(); BB != F.end(); ++BB)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); ++II)
        {
            if (IntrinsicInst* Intr = dyn_cast<IntrinsicInst>(II)) {
                if (Intr->getCalledFunction()->getIntrinsicID() == Intrinsic::gcroot) {
                    result++;
                }
            }
        }
    }
    return result;
}

bool LLSTPass::removeRedundantRoots(Function& F)
{
    InstructionSet toDelete;
    BasicBlock& entryBB = F.getEntryBlock();
    for (BasicBlock::iterator Instr = entryBB.begin(); Instr != entryBB.end(); ++Instr)
    {
        if (IntrinsicInst* createRootCall = dyn_cast<IntrinsicInst>(Instr))
        {
            if(createRootCall->getCalledFunction()->getIntrinsicID() == Intrinsic::gcroot)
            {
                if (! isa<ConstantPointerNull>(createRootCall->getArgOperand(1)))
                    continue; // this is a special case of stack objects that should be preserved
                    
                Value* holder = cast<Instruction>( createRootCall->getArgOperand(0)->stripPointerCasts() );
                bool onlyStoresToRoot = true;
                for(Value::use_iterator U = holder->use_begin(); U != holder->use_end() ; ++U) {
                    Instruction* I = cast<Instruction>(*U);
                    switch( I->getOpcode() ) {
                        case Instruction::Load: {
                            onlyStoresToRoot = false;
                            break;
                        }
                    }
                }
                if (onlyStoresToRoot) {
                    toDelete.insert(createRootCall);
                } else {
                    //TODO Find something else
                }
            }
        }
    }
    bool CFGChanged = !toDelete.empty();
    rootsRemoved += toDelete.size();
    for(InstructionSI I = toDelete.begin(); I != toDelete.end(); ++I) {
        (*I)->eraseFromParent();
    }
    
    //If there are no gc.root intrinsics why should we use GC for that function?
    if (getNumRoots(F) == 0)
        F.clearGC();
    
    return CFGChanged;
}