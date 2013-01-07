// g++ test_llvm_types.cpp `llvm-config-3.0 --cppflags --ldflags --libs` -ldl

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/IRReader.h>

using namespace llvm;

int main() {
    SMDiagnostic Err;
    std::auto_ptr<Module> M;
    Module* mod = 0;
    M.reset(ParseIRFile("llvm_types.ll", Err, getGlobalContext()));
    mod = M.get();
    StructType* TObject = mod->getTypeByName("struct.TObject");
    outs() << *TObject << "\n";
}

/*  example of C++ API struct generation:
    StructType* tSize = StructType::create(mod->getContext(), "struct.TSize");
    std::vector<Type*> tSize_fields;
    tSize_fields.push_back(IntegerType::get(mod->getContext(), 32)); //data
    tSize_fields.push_back(IntegerType::get(mod->getContext(), 32)); //relocated
    tSize_fields.push_back(IntegerType::get(mod->getContext(), 32)); //binary
    tSize_fields.push_back(IntegerType::get(mod->getContext(), 32)); //mask
    tSize->setBody(tSize_fields, false);
*/