#include <fstream>
#include <iostream>

#include "ast.h"
#include "globals.h"

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

// Parse Functions

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

ASTNode* ConditionalNode::tryParse(std::fstream &in) {
    std::vector<ASTNode*> children = {};
    ASTNode* node;

    while ((node = ASTNode::tryParse(in)) != nullptr) {
        children.push_back(node);
    }

    return new ConditionalNode(std::move(children));
}

ASTNode* ProgramNode::tryParse(std::fstream &in) {
    std::vector<ASTNode*> children = {};
    ASTNode* node;

    while ((node = ASTNode::tryParse(in)) != nullptr) {
        children.push_back(node);
    }

    return new ProgramNode(std::move(children));
}

// Debug Print Functions
void IncrementPtrNode::debugPrint() {
    std::cout << ">" << std::endl;
}

void DecrementPtrNode::debugPrint() {
    std::cout << "<" << std::endl;
}

void IncrementValNode::debugPrint() {
    std::cout << "+" << std::endl;
}

void DecrementValNode::debugPrint() {
    std::cout << "-" << std::endl;
}

void OutputNode::debugPrint() {
    std::cout << "." << std::endl;
}

void InputNode::debugPrint() {
    std::cout << "," << std::endl;
}

void ConditionalNode::debugPrint() {
    std::cout << "[" << std::endl;
    for (auto &child : children) {
        child->debugPrint();
    }
    std::cout << "]" << std::endl;
}

void ProgramNode::debugPrint() {
    for (auto &child : children) {
        child->debugPrint();
    }
}

// Util functions for code generation
static Value* getCurrPosition() {
    AllocaInst *position_var = NamedValues["position"];
    return Builder->CreateLoad(
        position_var->getAllocatedType(), 
        position_var, 
        "position"
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

// Code Generation Functions
void IncrementPtrNode::codeGen() { // >
    AllocaInst *ptr = NamedValues["pos"];

    Value *toAdd = ConstantInt::get(Type::getInt32Ty(*TheContext), 1);
    Value *currentVal = Builder->CreateLoad(ptr->getAllocatedType(), ptr, "pos");
    Value *newVal = Builder->CreateAdd(currentVal, toAdd, "new pos");
    Builder->CreateStore(newVal, ptr);
}

void DecrementPtrNode::codeGen() { // <
    AllocaInst *ptr = NamedValues["pos"];

    Value *toSub = ConstantInt::get(Type::getInt32Ty(*TheContext), 1);
    Value *currentVal = Builder->CreateLoad(ptr->getAllocatedType(), ptr, "pos");
    Value *newVal = Builder->CreateSub(currentVal, toSub, "new pos");
    Builder->CreateStore(newVal, ptr);
}

void IncrementValNode::codeGen() { // +
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

void DecrementValNode::codeGen() { // -
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

void OutputNode::codeGen() { // .
    FunctionType* outCharType = FunctionType::get(
        Type::getInt32Ty(*TheContext), 
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

void InputNode::codeGen() { // ,
    FunctionType* inCharType = FunctionType::get(
        Type::getInt32Ty(*TheContext), 
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

void ProgramNode::codeGen() {
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
        "position"
    );

    Value *initialValue = ConstantInt::get(Type::getInt64Ty(*TheContext), 0);
    Builder->CreateStore(initialValue, position);
    NamedValues["position"] = position;

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

void ConditionalNode::codeGen() {
    AllocaInst *positionVar= NamedValues["position"];
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
