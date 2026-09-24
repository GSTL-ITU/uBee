// Tokens to AST, one line at a time.
//
// Recovery is per line: on a parse error the parser reports it, skips to the
// end of the line, and carries on. A student with five typos should see all
// five, not just the first -- and because assembly lines are independent, that
// costs nothing.
#pragma once

#include "asm/ast.hpp"
#include "asm/diagnostic.hpp"
#include "asm/source.hpp"

namespace rv::as {

ParsedProgram parse(const SourceFile& source, DiagBag& diagnostics);

}  // namespace rv::as
