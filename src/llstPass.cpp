#define DEBUG_TYPE "llst"
#include <llstPass.h>

STATISTIC(rootLoadsRemoved, "Number of removed loads from gc.root protected pointers                                <<<<<<");
STATISTIC(rootsRemoved,     "Number of removed roots                                                                <<<<<<");

using namespace llvm;

typedef std::vector<Instruction*>   InstructionVector;
typedef std::set<Instruction*>      InstructionSet;
typedef InstructionSet::iterator    InstructionSI;

namespace {
    struct LLSTPass : public FunctionPass {
    private:
        std::set<std::string> m_GCFunctionNames;
        bool isPotentialGCFunction(std::string name);
        bool removeRootLoads(BasicBlock* B);
        bool removeRedundantRoots(Function* F);
        int  getNumRoots(Function* F);
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
    CFGChanged |= removeRedundantRoots(&F);
    return CFGChanged;
}

bool isLoadFromRoot(Instruction* Instr) {
    return true;
    ///TODO ensure Instr.getOperand(0) points to root
    
}

bool LLSTPass::isPotentialGCFunction(std::string name)
{
    return m_GCFunctionNames.find(name) != m_GCFunctionNames.end();
}


bool LLSTPass::removeRootLoads(BasicBlock* B)
{
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

int LLSTPass::getNumRoots(Function* F) {
    int result = 0;
    for (Function::iterator BB = F->begin(); BB != F->end(); BB++)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); II++)
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

bool LLSTPass::removeRedundantRoots(Function* F)
{
    InstructionSet toDelete;
    BasicBlock& entryBB = F->getEntryBlock();
    for (BasicBlock::iterator Instr = entryBB.begin(); Instr != entryBB.end(); Instr++ )
    {
        if (IntrinsicInst* createRootCall = dyn_cast<IntrinsicInst>(Instr))
        {
            if(createRootCall->getCalledFunction()->getIntrinsicID() == Intrinsic::gcroot)
            {
                Value* holder = cast<Instruction>( createRootCall->getArgOperand(0)->stripPointerCasts() );
                bool onlyStoresToRoot = true;
                for(Value::use_iterator U = holder->use_begin(); U != holder->use_end() ; U++) {
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
    for(InstructionSI I = toDelete.begin(); I != toDelete.end(); I++) {
        (*I)->eraseFromParent();
    }
    
    //If there are no gc.root intrinsics why should we use GC for that function?
    if (getNumRoots(F) == 0)
        F->clearGC();
    
    return CFGChanged;
}

#include <jit.h> //TObjectTypes
#include <llvm/Support/IRBuilder.h>
namespace {
    struct BrokenPointerPass : public FunctionPass {
    TObjectTypes m_baseTypes;
    public:
        virtual bool runOnFunction(Function &F) ;
        static char ID;
        BrokenPointerPass(): FunctionPass(ID) { }
        bool belongsToSmalltalkType(Type* type);
    };
}

char BrokenPointerPass::ID = 0;
static RegisterPass<BrokenPointerPass> LLSTBrokenPointerPass("load-debug", "Pass inserts checks for loads, if the class of load inst result points to null, a message will be reported", false /* Only looks at CFG */, false /* Analysis Pass */);

FunctionPass* createBrokenPointerPass() {
  return new BrokenPointerPass();
}

bool BrokenPointerPass::belongsToSmalltalkType(Type* type)
{
    if (   type == m_baseTypes.block->getPointerTo()
        || type == m_baseTypes.byteObject->getPointerTo()
        || type == m_baseTypes.process->getPointerTo()
        || type == m_baseTypes.object->getPointerTo()
        || type == m_baseTypes.objectArray->getPointerTo()
        || type == m_baseTypes.symbol->getPointerTo()
        || type == m_baseTypes.symbolArray->getPointerTo()
        || type == m_baseTypes.dictionary->getPointerTo()
        || type == m_baseTypes.method->getPointerTo()
        || type == m_baseTypes.context->getPointerTo()
        || type == m_baseTypes.klass->getPointerTo()
    )
        return true;
    return false;
}


bool BrokenPointerPass::runOnFunction(Function& F)
{
    Module* jitModule = F.getParent();
    m_baseTypes.initializeFromModule(jitModule);
    IRBuilder<>* builder = new IRBuilder<>(F.begin());
    FunctionType* _printfType = FunctionType::get(builder->getInt32Ty(), builder->getInt8PtrTy(), true);
    Constant*     _printf     = jitModule->getOrInsertFunction("printf", _printfType);
    Value* BrokenPointerMessage = builder->CreateGlobalStringPtr("\npointer is broken\n");
    Value* BrokenSelfMessage = builder->CreateGlobalStringPtr("\nself is broken\n");
    Function* isSmallInteger = jitModule->getFunction("isSmallInteger");
    Function* getObjectField = jitModule->getFunction("getObjectField");
    Function* getObjectClass = jitModule->getFunction("getObjectClass");
    
    InstructionVector Loads;
    for (Function::iterator BB = F.begin(); BB != F.end(); BB++)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); II++)
        {
            if (LoadInst* Load = dyn_cast<LoadInst>(II)) {
                Loads.push_back(Load);
            }
        }
    }
    
    for(size_t i = 0; i < Loads.size(); i++)
    {
        LoadInst* Load = dyn_cast<LoadInst>(Loads[i]);
        if (belongsToSmalltalkType( Load->getType() )) {
        
            //split BB right after load inst. The new BB contains code that will be executed if pointer is OK
            BasicBlock* PointerIsOkBB = Load->getParent()->splitBasicBlock(++((BasicBlock::iterator) Load));
            BasicBlock* PointerIsBrokenBB = BasicBlock::Create(jitModule->getContext(), "", &F, PointerIsOkBB);
            BasicBlock* PointerIsNotSmallIntBB = BasicBlock::Create(jitModule->getContext(), "", &F, PointerIsBrokenBB);
            
            Instruction* branchToPointerIsOkBB = ++((BasicBlock::iterator) Load);
            //branchToPointerIsOkBB is created by splitBasicBlock() just after load inst
            //We force builder to insert instructions before branchToPointerIsOkBB
            builder->SetInsertPoint(branchToPointerIsOkBB);
            
            //If pointer to class is null, jump to PointerIsBroken, otherwise to PointerIsOkBB
            Value* objectPtr = builder->CreateBitCast( Load, m_baseTypes.object->getPointerTo());
            
            Value* isSmallInt = builder->CreateCall(isSmallInteger, objectPtr);
            builder->CreateCondBr(isSmallInt, PointerIsOkBB, PointerIsNotSmallIntBB);
            
            builder->SetInsertPoint(PointerIsNotSmallIntBB);
            Value* klassPtr = builder->CreateCall(getObjectClass, objectPtr);
            Value* pointerIsNull = builder->CreateICmpEQ(klassPtr, ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) );
            builder->CreateCondBr(pointerIsNull, PointerIsBrokenBB, PointerIsOkBB);
            
            branchToPointerIsOkBB->eraseFromParent(); //We don't need it anymore
            
            builder->SetInsertPoint(PointerIsBrokenBB);
            builder->CreateCall(_printf, BrokenPointerMessage);
            builder->CreateBr(PointerIsOkBB);
        }
    }

    InstructionVector Calls;
    for (Function::iterator BB = F.begin(); BB != F.end(); BB++)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); II++)
        {
            if (CallInst* Call = dyn_cast<CallInst>(II)) {
                if (Call->getCalledFunction()->getName() == "sendMessage")
                    Calls.push_back(Call);
            }
        }
    }
    
    for(size_t i = 0; i < Calls.size(); i++)
    {
        CallInst* Call = dyn_cast<CallInst>(Calls[i]);
        
        BasicBlock* PointerIsOkBB = Call->getParent()->splitBasicBlock(((BasicBlock::iterator) Call));
        BasicBlock* PointerIsBrokenBB = BasicBlock::Create(jitModule->getContext(), "", &F, PointerIsOkBB);
        BasicBlock* PointerIsNotSmallIntBB = BasicBlock::Create(jitModule->getContext(), "", &F, PointerIsBrokenBB);
        
        Instruction* branchToPointerIsOkBB = & PointerIsOkBB->getSinglePredecessor()->back();
        builder->SetInsertPoint(branchToPointerIsOkBB);
        
        
        Value* argsPtr = builder->CreateBitCast( Call->getArgOperand(2), m_baseTypes.object->getPointerTo());
        Value* self = builder->CreateCall2(getObjectField, argsPtr, builder->getInt32(0));
        
        Value* isSmallInt = builder->CreateCall(isSmallInteger, self);
        builder->CreateCondBr(isSmallInt, PointerIsOkBB, PointerIsNotSmallIntBB);
        
        
        builder->SetInsertPoint(PointerIsNotSmallIntBB);
        Value* klassPtr = builder->CreateCall(getObjectClass, self);
        Value* pointerIsNull = builder->CreateICmpEQ(klassPtr, ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) );
        builder->CreateCondBr(pointerIsNull, PointerIsBrokenBB, PointerIsOkBB);
        branchToPointerIsOkBB->eraseFromParent();
        
        builder->SetInsertPoint(PointerIsBrokenBB);
        builder->CreateCall(_printf, BrokenSelfMessage);
        builder->CreateBr(PointerIsOkBB);
    }
    
    //outs() << F;
    delete builder;
    return true;
}
