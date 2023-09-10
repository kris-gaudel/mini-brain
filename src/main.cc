#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <stdio.h>

#include "llvm/IR/IRBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "ast.h"
#include "globals.h"

using namespace llvm;

// Initalize LLVM
static void llvmInit() {
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("mini-brain", *TheContext);
    Builder = std::make_unique<IRBuilder<>>(*TheContext);

    // Compiler optimizations provided by LLVM
    TheFPM->add(createInstructionCombiningPass()); // Peephole optimizations
    TheFPM->add(createReassociatePass()); // Reassociate expressions
    TheFPM->add(createGVNPass()); // Eliminate common subexpressions
    TheFPM->add(createCFGSimplificationPass()); // Simplify control flow graph by deleting unreachable code
    TheFPM->doInitialization();

    // Initialize LLVM target and asm printer
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
}

int main() {
    std::fstream in("program.bf");
    if (!in.is_open()) {
        std::cout << "Could not open file" << std::endl;
        return 1;
    }

    ASTNode *root = ProgramNode::tryParse(in);
    if (root == nullptr) {
        std::cout << "Could not parse file" << std::endl;
        return 1;
    }

    in.close();

    llvmInit();
    root->codeGen();

    Function *main = TheModule->getFunction("main");
    if (main == nullptr) {
        std::cout << "Could not find main function" << std::endl;
        return 1;
    }

    TheFPM->run(*main);
    TheModule->print(outs(), nullptr);

    return 0;
}
