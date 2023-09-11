#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

// Global variables
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, AllocaInst *> NamedValues;
static std::unique_ptr<legacy::FunctionPassManager> TheFPM;

const unsigned int TAPE_LENGTH = 30000;

// Util functions for code generation
static Value* getCurrPosition() {
    AllocaInst *position_var = NamedValues["pos"];
    return Builder->CreateLoad(
        position_var->getAllocatedType(), 
        position_var, 
        "pos"
    );
}

static Value* getCurrTapeCellPtr() {
    AllocaInst *tape = NamedValues["tape"];

    // Get the address of the cell value at the current positin
    return Builder->CreateGEP(
        tape->getAllocatedType(), 
        tape, 
        {
            ConstantInt::get(Type::getInt8Ty(*TheContext), 0), 
            getCurrPosition()
        },
        "tape cell ptr"
    );
}

static Value* getCurrTapeValue() {
    Value* ptr = getCurrTapeCellPtr();
    return Builder->CreateLoad(
        Type::getInt8Ty(*TheContext), 
        ptr
    );
}

// AST Classes

class ASTNode {
    public:
        static ASTNode* tryParse(std::fstream &in);

        virtual void debugPrint() = 0;
        virtual void codeGen() = 0;
};

// >
class IncrementPtrNode : public ASTNode {
    void debugPrint() override {
        std::cout << ">" << std::endl;
    }
    void codeGen() override {
        AllocaInst *ptr = NamedValues["pos"];

        Value *toAdd = ConstantInt::get(Type::getInt64Ty(*TheContext), 1);
        Value *currentVal = Builder->CreateLoad(ptr->getAllocatedType(), ptr, "pos");
        Value *newVal = Builder->CreateAdd(currentVal, toAdd, "new position");
        Builder->CreateStore(newVal, ptr);
    }
};

// <
class DecrementPtrNode : public ASTNode {
    void debugPrint() override {
        std::cout << "<" << std::endl;
    }

    void codeGen() override {
        AllocaInst *ptr = NamedValues["pos"];

        Value *toSub = ConstantInt::get(Type::getInt64Ty(*TheContext), 1);
        Value *currentVal = Builder->CreateLoad(ptr->getAllocatedType(), ptr, "pos");
        Value *newVal = Builder->CreateSub(currentVal, toSub, "new position");
        Builder->CreateStore(newVal, ptr);
    }
};

// +
class IncrementValNode : public ASTNode {
    void debugPrint() override {
        std::cout << "+" << std::endl;
    }
    void codeGen() override {
        Value *tapeCellPtr = getCurrTapeCellPtr();
        Value *tapeCell = Builder->CreateLoad(
            Type::getInt8Ty(*TheContext),
            tapeCellPtr, 
            "tape cell"
        );

        Value *toAdd = ConstantInt::get(Type::getInt8Ty(*TheContext), 1);
        Value *newVal = Builder->CreateAdd(tapeCell, toAdd, "new tape cell value");

        Builder->CreateStore(newVal, tapeCellPtr);
    }
};

// -
class DecrementValNode : public ASTNode {
    void debugPrint() override {
        std::cout << "-" << std::endl;
    }

    void codeGen() override {
        Value *tapeCellPtr = getCurrTapeCellPtr();
        Value *tapeCell = Builder->CreateLoad(
            Type::getInt8Ty(*TheContext),
            tapeCellPtr, 
            "tape cell"
        );

        Value *toSub = ConstantInt::get(Type::getInt8Ty(*TheContext), 1);
        Value *newVal = Builder->CreateSub(tapeCell, toSub, "new tape cell value");

        Builder->CreateStore(newVal, tapeCellPtr);
    }
};

// .
class OutputNode : public ASTNode {
    void debugPrint() override {
        std::cout << "." << std::endl;
    }

    void codeGen() override {
        FunctionType* outCharType = FunctionType::get(
            Type::getInt64Ty(*TheContext), 
            {Type::getInt8Ty(*TheContext)}, 
            false
        );

        FunctionCallee outChar = TheModule->getOrInsertFunction("putchar", outCharType);

        Value *tapeCell = getCurrTapeValue();

        Builder->CreateCall(
            outCharType,
            outChar.getCallee(),
            {tapeCell},
            "putchar()"
        );
    }
};

// ,
class InputNode : public ASTNode {
    void debugPrint() override {
        std::cout << "," << std::endl;
    }

    void codeGen() override {
        FunctionType* inCharType = FunctionType::get(
            Type::getInt64Ty(*TheContext), 
            {}, 
            false
        );

        FunctionCallee inChar = TheModule->getOrInsertFunction("getchar", inCharType);

        Value *c = Builder->CreateCall(
            inCharType,
            inChar.getCallee(),
            {},
            "getchar()"
        );

        Value *truncated_char = Builder->CreateIntCast(
            c, 
            Type::getInt8Ty(*TheContext), 
            true
        );

        AllocaInst *tape = NamedValues["tape"];
        Value *ptr = getCurrTapeCellPtr();
        Builder->CreateStore(truncated_char, ptr);
    }
};

// For nodes that contain other nodes
class ScopeNode: public ASTNode {
    protected:
        std::vector<ASTNode*> children;

    public:
        explicit ScopeNode(std::vector<ASTNode*> children): children(children) {};
};

// [...]
class ConditionalNode : public ScopeNode {
    public:
        ConditionalNode(std::vector<ASTNode*> children): ScopeNode(children) {};

        static ASTNode* tryParse(std::fstream &in) {
            std::vector<ASTNode*> children = {};
            ASTNode* node;

            while ((node = ASTNode::tryParse(in)) != nullptr) {
                children.push_back(node);
            }

            return new ConditionalNode(std::move(children));
        }

        void debugPrint() override {
            std::cout << "[" << std::endl;
            for (auto &child : children) {
                child->debugPrint();
            }
            std::cout << "]" << std::endl;
        }

        void codeGen() override {
            AllocaInst *positionVar= NamedValues["pos"];
            AllocaInst *tape = NamedValues["tape"];

            BasicBlock *baseBlock = Builder->GetInsertBlock();
            Function *theFunction = baseBlock->getParent();

            Value* startCondition = Builder->CreateICmpNE(
                getCurrTapeValue(), 
                ConstantInt::get(Type::getInt8Ty(*TheContext), 0)
            );

            BasicBlock *groupContent= BasicBlock::Create(*TheContext, "group content", theFunction);
            BasicBlock *merge = BasicBlock::Create(*TheContext, "merge", theFunction);

            Builder->CreateCondBr(startCondition, groupContent, merge);

            Builder->SetInsertPoint(groupContent);

            for (auto &child : children) {
                child->codeGen();
            }

            Value* endCondition = Builder->CreateICmpNE(
                getCurrTapeValue(), 
                ConstantInt::get(Type::getInt8Ty(*TheContext), 0)
            );

            Builder->CreateCondBr(endCondition, groupContent, merge);
            Builder->SetInsertPoint(merge);
        }
};

// Program (root node)
class ProgramNode: public ScopeNode {
    public:
        ProgramNode(std::vector<ASTNode*> children): ScopeNode(children) {};

        static ASTNode* tryParse(std::fstream &in) {
            std::vector<ASTNode*> children = {};
            ASTNode* node;

            while ((node = ASTNode::tryParse(in)) != nullptr) {
                children.push_back(node);
            }

            return new ProgramNode(std::move(children));
        }

        void debugPrint() override {
            for (auto &child : children) {
                child->debugPrint();
            }
        }


        void codeGen() override {
            FunctionType *mainType = FunctionType::get(Builder->getVoidTy(), false);
            Function *mainFunc = Function::Create(
                mainType, 
                GlobalValue::ExternalLinkage, 
                "main", 
                *TheModule
            );

            BasicBlock *mainBlock = BasicBlock::Create(*TheContext, "entry", mainFunc);
            Builder->SetInsertPoint(mainBlock);

            AllocaInst* position = Builder->CreateAlloca(
                Type::getInt64Ty(*TheContext), 
                nullptr, 
                "pos"
            );

            Value *initialValue = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
            Builder->CreateStore(initialValue, position);
            NamedValues["pos"] = position;

            Type *tapeType = ArrayType::get(Type::getInt8Ty(*TheContext), TAPE_LENGTH);
            AllocaInst* tape = Builder->CreateAlloca(
                tapeType, 
                nullptr, 
                "tape"
            );

            Constant* initial_tape = ConstantAggregateZero::get(tapeType);
            Builder->CreateStore(initial_tape, tape);
            NamedValues["tape"] = tape;

            for (auto &child : children) {
                child->codeGen();
            }
            Builder->CreateRet(nullptr);
            verifyFunction(*mainFunc);
        }
};

// AST Node parsing
ASTNode* ASTNode::tryParse(std::fstream &in) {
    while(true) {
        char c;
        if (!(in >> c)) {
            return nullptr;
        }

        switch(c) {
            case '+':
                return new IncrementValNode();
            case '-':
                return new DecrementValNode();
            case '>':
                return new IncrementPtrNode();
            case '<':
                return new DecrementPtrNode();
            case '.':
                return new OutputNode();
            case ',':
                return new InputNode();
            case '[':
                return ConditionalNode::tryParse(in);
            case ']':
                return nullptr;
            default:
                break;
        }
    }
}


// Initalize LLVM
static void llvmInit() {
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("mini-brain", *TheContext);
    Builder = std::make_unique<IRBuilder<>>(*TheContext);
    TheFPM = std::make_unique<legacy::FunctionPassManager>(TheModule.get());

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
