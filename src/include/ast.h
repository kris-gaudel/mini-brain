#ifndef AST_H
#define AST_H

#include <vector>
#include <fstream>

class ASTNode {
    public:
        static ASTNode* tryParse(std::fstream &in);

        virtual void debugPrint() = 0;
        virtual void codeGen() = 0;
};

// >
class IncrementPtrNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
};

// <
class DecrementPtrNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
};

// +
class IncrementValNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
};

// -
class DecrementValNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
};

// .
class OutputNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
};

// ,
class InputNode : public ASTNode {
    void debugPrint() override;
    void codeGen() override;
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

        static ASTNode* tryParse(std::fstream &in);
        void debugPrint() override;
        void codeGen() override;
};

// Program (root node)
class ProgramNode: public ScopeNode {
    public:
        ProgramNode(std::vector<ASTNode*> children): ScopeNode(children) {};

        static ASTNode* tryParse(std::fstream &in);
        void debugPrint() override;
        void codeGen() override;
};


#endif
