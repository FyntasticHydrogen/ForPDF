#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <memory>
#include <vector>

int main() {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);
    llvm::IRBuilder<> builder(*context);

    llvm::Type* floatType = builder.getFloatTy();
    std::vector<llvm::Type*> paramTypes = {floatType, floatType, floatType, floatType};
    llvm::FunctionType* funcType = llvm::FunctionType::get(floatType, paramTypes, false);

    llvm::Function* vec2AddFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "Vec2Add",
        module.get()
    );

    auto argIt = vec2AddFunc->arg_begin();
    llvm::Value* x1 = argIt++; x1->setName("x1");
    llvm::Value* y1 = argIt++; y1->setName("y1");
    llvm::Value* x2 = argIt++; x2->setName("x2");
    llvm::Value* y2 = argIt++; y2->setName("y2");

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", vec2AddFunc);
    builder.SetInsertPoint(entryBB);

    llvm::Value* xSum = builder.CreateFAdd(x1, x2, "x_sum");
    llvm::Value* ySum = builder.CreateFAdd(y1, y2, "y_sum");
    llvm::Value* totalSum = builder.CreateFAdd(xSum, ySum, "total_sum");

    builder.CreateRet(totalSum);

    if (llvm::verifyFunction(*vec2AddFunc, &llvm::outs())) {
        llvm::errs() << "HATA: Vec2Add fonksiyonu LLVM IR kurallarina uymuyor!\n";
        return 1;
    }

    llvm::outs() << "=== Oyun Dili Vec2Add LLVM IR Ciktisi ===\n";
    module->print(llvm::outs(), nullptr);

    return 0;
}
