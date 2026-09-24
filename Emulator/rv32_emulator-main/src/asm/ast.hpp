// The parse tree.
//
// Deliberately shallow: assembly is line-structured, so this is a vector of
// lines rather than a tree. Only expressions nest, and they live in an arena
// indexed by int so that adding a node can never dangle a pointer held by a
// half-built parent.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asm/directive.hpp"
#include "asm/pseudo.hpp"
#include "asm/source.hpp"
#include "asm/symtab.hpp"
#include "isa/isa.hpp"

namespace rv::as {

struct Expr {
    enum class Kind : u8 {
        Number,
        Symbol,
        Dot,      // '.' -- the address of the current line
        Unary,    // op applied to lhs
        Binary,   // lhs op rhs
        HiReloc,  // %hi(lhs): the upper 20 bits, biased so %lo can be negative
        LoReloc,  // %lo(lhs): the low 12 bits, sign-extended
    };

    Kind kind = Kind::Number;
    i64 value = 0;           // Number
    std::string_view name;   // Symbol
    char op = '\0';          // Unary and Binary: + - * / % & | ^ ~ L(<<) R(>>)
    int lhs = -1;
    int rhs = -1;
    SourceSpan span;
};

/// Expression storage for one parse. Nodes are referred to by index.
class ExprArena {
public:
    int add(Expr node) {
        nodes_.push_back(node);
        return static_cast<int>(nodes_.size()) - 1;
    }
    const Expr& operator[](int index) const { return nodes_[static_cast<std::size_t>(index)]; }
    bool valid(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < nodes_.size();
    }
    std::size_t size() const { return nodes_.size(); }
    void clear() { nodes_.clear(); }

private:
    std::vector<Expr> nodes_;
};

struct Operand {
    enum class Kind : u8 {
        Register,    // a0
        Expression,  // 42, label, sym + 4, %hi(msg)
        Memory,      // 8(sp) -- `expr` is the offset, `reg` the base
        Text,        // "hello" in a .asciz
    };

    Kind kind = Kind::Expression;
    RegIdx reg = 0;
    int expr = -1;
    std::string text;
    SourceSpan span;
};

struct AsmLine {
    enum class Kind : u8 {
        Empty,        // blank, or labels only
        Instruction,  // a real instruction from instr_table.def
        Pseudo,       // a pseudo-instruction
        Directive,
        /// Failed to parse, but looked like an instruction. Layout still
        /// reserves a word for it so that the addresses of everything after it
        /// stay right -- otherwise one typo shifts the whole program and turns
        /// every later branch into a spurious second error.
        Error,
    };

    Kind kind = Kind::Empty;
    u32 line = 0;

    /// Several labels may precede one item, all naming the same address.
    std::vector<std::string_view> labels;
    std::vector<SourceSpan> label_spans;

    isa::InstrId instr = isa::kInvalidInstr;
    PseudoId pseudo = kInvalidPseudo;
    DirectiveId directive = kInvalidDirective;

    std::vector<Operand> operands;

    /// Span of the mnemonic or directive itself, for precise error carets.
    SourceSpan mnemonic_span;
    std::string_view mnemonic;

    // Filled in by pass 1.
    Addr addr = 0;
    u8 words = 0;
    Space space = Space::Text;
};

struct ParsedProgram {
    std::vector<AsmLine> lines;
    ExprArena exprs;
};

}  // namespace rv::as
