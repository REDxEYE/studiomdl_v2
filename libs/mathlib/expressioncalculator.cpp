//========= Copyright c 1996-2008, Valve Corporation, All rights reserved. ============//

#include "tier0/platform.h"
#include <cmath>
#include <cstdlib>

#include "mathlib/mathlib.h"
#include "mathlib/noise.h"
#include "mathlib/vector.h"

#include "mathlib/expressioncalculator.h"

#include <ranges>

#include "studiomdl/studiomdl_log.hpp"
#include "tier1/strtools.h"


//-----------------------------------------------------------------------------
// Parsing helper methods
//-----------------------------------------------------------------------------
static bool ParseLiteral(std::string_view &expr, float &value) {
    // Every view handed to the parser is a suffix of CExpressionCalculator::m_expr, so it is NUL-terminated and
    // strtod keeps the original semantics (leading whitespace, hex, inf/nan, out-of-range -> HUGE_VAL).
    Assert(expr.data()[expr.size()] == '\0');
    char *end = nullptr;
    value = static_cast<float>(strtod(expr.data(), &end));
    const size_t consumed = end - expr.data();
    expr.remove_prefix(consumed);
    return consumed != 0;
}

static bool ParseString(std::string_view &expr, const std::string_view str) {
    const std::string_view startExpr = expr;
    std::string_view curExpr = expr;
    while (!curExpr.empty() && (curExpr[0] == ' ' || curExpr[0] == '\t'))
        curExpr = curExpr.substr(1); // skip whitespace

    if (const auto tmp = string_after_prefix(curExpr, str)) {
        expr = tmp.value();
        return true;
    }
    expr = startExpr;
    return false;
}

static bool ParseStringList(std::string_view &expr, const std::vector<std::string> &parts, size_t &matchIndex) {
    for (auto [index, op]: parts | std::views::enumerate | std::views::reverse) {
        if (ParseString(expr, op)) {
            matchIndex = index;
            return true;
        }
    }
    return false;
}

namespace {
    class ParseState_t {
    public:
        ParseState_t(const std::vector<float> &stack, const std::string_view expr)
            : m_stacksize(stack.size()), m_startingExpr(expr) {
        }

        void Reset(std::vector<float> &stack, std::string_view &expr) const {
            Assert(m_stacksize <= stack.size());
            stack.resize(m_stacksize);
            expr = m_startingExpr;
        }

    private:
        uint32_t m_stacksize;
        std::string_view m_startingExpr;
    };
}


void CExpressionCalculator::SetVariable(const std::string_view var, float value) {
    if (const auto index = std::ranges::find(m_varNames, var); index != m_varNames.end()) {
        m_varValues[index - m_varNames.begin()] = value;
    }
    else {
        m_varNames.emplace_back(var);
        m_varValues.emplace_back(value);
    }
}

void CExpressionCalculator::SetVariable(const size_t variableIndex, const float value) {
    m_varValues[variableIndex] = value;
}

bool CExpressionCalculator::Evaluate(float &value) {
    m_bIsBuildingArgumentList = false;
    m_stack.clear();
    std::string_view pExpr = m_expr;
    const bool success = ParseExpr(pExpr);
    if (success && m_stack.size() == 1) {
        value = m_stack.back();
        return true;
    }

    value = 0.0f;
    return false;
}


//-----------------------------------------------------------------------------
// Builds a list of variable names from the expression
//-----------------------------------------------------------------------------
bool CExpressionCalculator::BuildVariableListFromExpression() {
    m_bIsBuildingArgumentList = true;
    m_stack.clear();
    std::string_view pExpr = m_expr;

    const bool success = ParseExpr(pExpr);
    m_bIsBuildingArgumentList = false;
    if (!success || m_stack.size() != 1) {
        m_varNames.clear();
        return false;
    }
    return true;
}


//-----------------------------------------------------------------------------
// Iterate over variables
//-----------------------------------------------------------------------------
size_t CExpressionCalculator::VariableCount() const {
    return m_varNames.size();
}

std::string_view CExpressionCalculator::VariableName(const size_t index) {
    return m_varNames[index];
}


bool CExpressionCalculator::ParseExpr(std::string_view &expr) {
    return ParseConditional(expr);
}

bool CExpressionCalculator::ParseConditional(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseOr(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    ParseState_t ps1(m_stack, expr);
    if (ParseString(expr, "?") &&
        ParseExpr(expr) &&
        ParseString(expr, ":") &&
        ParseExpr(expr)) {
        float f3 = m_stack.back();
        m_stack.pop_back();
        float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        m_stack.emplace_back(f1 != 0.0f ? f2 : f3);
        return true; // and matched
    }
    ps1.Reset(m_stack, expr);
    return true; // equality (or lower) matched
}

bool CExpressionCalculator::ParseOr(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseAnd(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    ParseState_t ps1(m_stack, expr);
    if (ParseString(expr, "||") &&
        ParseOr(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        m_stack.emplace_back((f1 != 0.0f) || (f2 != 0.0f) ? 1 : 0);
        return true; // and matched
    }
    ps1.Reset(m_stack, expr);
    return true; // equality (or lower) matched
}

bool CExpressionCalculator::ParseAnd(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseEquality(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    ParseState_t ps1(m_stack, expr);
    if (ParseString(expr, "&&") &&
        ParseAnd(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        m_stack.emplace_back((f1 != 0.0f) && (f2 != 0.0f) ? 1 : 0);
        return true; // and matched
    }
    ps1.Reset(m_stack, expr);
    return true; // equality (or lower) matched
}

bool CExpressionCalculator::ParseEquality(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseLessGreater(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    const std::vector<std::string> pOps = {"==", "!="};

    ParseState_t ps1(m_stack, expr);
    if (size_t opIndex; ParseStringList(expr, pOps, opIndex) &&
                        ParseEquality(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (opIndex) {
            case 0: // ==
                m_stack.emplace_back(f1 == f2 ? 1 : 0);
                break;
            case 1: // !=
                m_stack.emplace_back(f1 != f2 ? 1 : 0);
                break;
            default:
                MdlError("Invalid equality operator");
        }
        return true; // equality matched
    }
    ps1.Reset(m_stack, expr);
    return true; // lessgreater (or lower) matched
}

bool CExpressionCalculator::ParseLessGreater(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseAddSub(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    const std::vector<std::string> pOps = {"<", ">", "<=", ">="};

    ParseState_t ps1(m_stack, expr);
    if (size_t opIndex; ParseStringList(expr, pOps, opIndex) && ParseLessGreater(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (opIndex) {
            case 0: // <
                m_stack.emplace_back(f1 < f2 ? 1 : 0);
                break;
            case 1: // >
                m_stack.emplace_back(f1 > f2 ? 1 : 0);
                break;
            case 2: // <=
                m_stack.emplace_back(f1 <= f2 ? 1 : 0);
                break;
            case 3: // >=
                m_stack.emplace_back(f1 >= f2 ? 1 : 0);
                break;
            default:
                MdlError("Invalid inequality operator");
        }
        return true; // inequality matched
    }
    ps1.Reset(m_stack, expr);
    return true; // addsub (or lower) matched
}

bool CExpressionCalculator::ParseAddSub(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseDivMul(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    const std::vector<std::string> pOps = {"+", "-"};

    ParseState_t ps1(m_stack, expr);
    if (size_t opIndex; ParseStringList(expr, pOps, opIndex) && ParseAddSub(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (opIndex) {
            case 0: // +
                m_stack.emplace_back(f1 + f2);
                break;
            case 1: // -
                m_stack.emplace_back(f1 - f2);
                break;
            default:
                MdlError("Invalid addition/subtraction operator");
        }
        return true; // addsub matched
    }
    ps1.Reset(m_stack, expr);
    return true; // divmul (or lower) matched
}

bool CExpressionCalculator::ParseDivMul(std::string_view &expr) {
    ParseState_t ps0(m_stack, expr);
    if (!ParseUnary(expr)) {
        ps0.Reset(m_stack, expr);
        return false; // nothing matched
    }

    const std::vector<std::string> pOps = {"*", "/", "%"};

    ParseState_t ps1(m_stack, expr);
    if (size_t opIndex; ParseStringList(expr, pOps, opIndex) && ParseDivMul(expr)) {
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (opIndex) {
            case 0: // *
                m_stack.emplace_back(f1 * f2);
                break;
            case 1: // /
                m_stack.emplace_back(f1 / f2);
                break;
            case 2: // %
                m_stack.emplace_back(fmod(f1, f2));
                break;
            default:
                MdlError("Invalid Mul/Div operator");
        }
        return true; // divmul matched
    }
    ps1.Reset(m_stack, expr);
    return true; // unary (or lower) matched
}

bool CExpressionCalculator::ParseUnary(std::string_view &expr) {
    ParseState_t ps(m_stack, expr);

    const std::vector<std::string> pOps = {"+", "-", "!"};

    if (size_t opIndex; ParseStringList(expr, pOps, opIndex) && ParseUnary(expr)) {
        float f1 = m_stack.back();
        m_stack.pop_back();
        switch (opIndex) {
            case 0: // +
                m_stack.emplace_back(f1);
                break;
            case 1: // -
                m_stack.emplace_back(-f1);
                break;
            case 2: // !
                m_stack.emplace_back(f1 == 0 ? 1 : 0);
                break;
            default:
                MdlError("Invalid Unary operator");
        }
        return true;
    }

    ps.Reset(m_stack, expr);
    if (ParsePrimary(expr))
        return true;

    ps.Reset(m_stack, expr);
    return false;
}

bool CExpressionCalculator::ParsePrimary(std::string_view &expr) {
    ParseState_t ps(m_stack, expr);

    float value = 0.0f;
    if (ParseLiteral(expr, value)) {
        m_stack.emplace_back(value);
        return true;
    }

    ps.Reset(m_stack, expr);
    if (size_t varIndex; ParseStringList(expr, m_varNames, varIndex)) {
        m_stack.emplace_back(m_varValues[varIndex]);
        return true;
    }

    ps.Reset(m_stack, expr);
    if (ParseString(expr, "(") &&
        ParseExpr(expr) &&
        ParseString(expr, ")")) {
        return true;
    }

    ps.Reset(m_stack, expr);
    if (Parse1ArgFunc(expr) ||
        Parse2ArgFunc(expr) ||
        Parse3ArgFunc(expr) ||
        //		Parse4ArgFunc( expr ) ||
        Parse5ArgFunc(expr)) {
        return true;
    }

    // If we're parsing it to discover names of variable names, add them here
    if (!m_bIsBuildingArgumentList)
        return false;

    // Variables can't start with a number
    if (!expr.empty() && V_isdigit(expr[0]))
        return false;

    const std::string_view pStart = expr;
    while (!expr.empty() && (V_isalnum(expr[0]) || expr[0] == '_')) {
        expr = expr.substr(1);
    }

    const size_t len = expr.data() - pStart.data();
    const std::string_view variableName = pStart.substr(0, len);

    SetVariable(variableName, 0.0f);
    m_stack.emplace_back(0.0f);
    return true;
}

/*
dtor(d) : converts degrees to radians
rtod(r) : converts radians to degrees

abs(a)     : absolute value
floor(a)   : rounds down to the nearest integer
ceiling(a) : rounds up to the nearest integer
round(a)   : rounds to the nearest integer
sgn(a)     : if a < 0 returns -1 else 1
sqr(a)     : returns a * a
sqrt(a)    : returns sqrt(a)

sin(a)     : sin(a), a is in degrees
asin(a)    : asin(a) returns degrees
cos(a)     : cos(a), a is in degrees
acos(a)    : acos(a) returns degrees
tan(a)     : tan(a), a is in degrees

exp(a)   : returns the exponential function of a
log(a)   : returns the natural logaritm of a
*/
bool CExpressionCalculator::Parse1ArgFunc(std::string_view &expr) {
    const std::vector<std::string> pFuncs =
    {
        "abs", "sqr", "sqrt", "sin", "asin", "cos", "acos", "tan",
        "exp", "log", "dtor", "rtod", "floor", "ceiling", "round", "sign"
    };
    if (size_t funcIndex; ParseStringList(expr, pFuncs, funcIndex) &&
                          ParseString(expr, "(") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ")")) {
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (funcIndex) {
            case 0: // abs
                m_stack.emplace_back(fabs(f1));
                break;
            case 1: // sqr
                m_stack.emplace_back(f1 * f1);
                break;
            case 2: // sqrt
                m_stack.emplace_back(sqrt(f1));
                break;
            case 3: // sin
                m_stack.emplace_back(sin(f1));
                break;
            case 4: // asin
                m_stack.emplace_back(asin(f1));
                break;
            case 5: // cos
                m_stack.emplace_back(cos(f1));
                break;
            case 6: // acos
                m_stack.emplace_back(acos(f1));
                break;
            case 7: // tan
                m_stack.emplace_back(tan(f1));
                break;
            case 8: // exp
                m_stack.emplace_back(exp(f1));
                break;
            case 9: // log
                m_stack.emplace_back(log(f1));
                break;
            case 10: // dtor
                m_stack.emplace_back(DEG2RAD(f1));
                break;
            case 11: // rtod
                m_stack.emplace_back(RAD2DEG(f1));
                break;
            case 12: // floor
                m_stack.emplace_back(floor(f1));
                break;
            case 13: // ceiling
                m_stack.emplace_back(ceil(f1));
                break;
            case 14: // round
                m_stack.emplace_back(floor(f1 + 0.5f));
                break;
            case 15: // sign
                m_stack.emplace_back(f1 >= 0.0f ? 1.0f : -1.0f);
                break;
            default:
                MdlError("Invalid Binary 1 arg function");
        }
        return true;
    }
    return false;
}

/*
min(a,b)     : if a<b returns a else b
max(a,b)     : if a>b returns a else b
atan2(a,b) : atan2(a/b) returns degrees
pow(a,b) : function returns a raised to the power of b
*/
bool CExpressionCalculator::Parse2ArgFunc(std::string_view &expr) {
    const std::vector<std::string> pFuncs = {"min", "max", "atan2", "pow"};
    if (size_t funcIndex; ParseStringList(expr, pFuncs, funcIndex) &&
                          ParseString(expr, "(") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ",") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ")")) {
        float f2 = m_stack.back();
        m_stack.pop_back();
        float f1 = m_stack.back();
        m_stack.pop_back();
        switch (funcIndex) {
            case 0: // min
                m_stack.emplace_back(MIN(f1, f2));
                break;
            case 1: // max
                m_stack.emplace_back(MAX(f1, f2));
                break;
            case 2: // atan2
                m_stack.emplace_back(atan2(f1, f2));
                break;
            case 3: // pow
                m_stack.emplace_back(pow(f1, f2));
                break;
            default:
                MdlError("Invalid Binary 2 arg function");
        }
        return true;
    }
    return false;
}

/*
inrange(x,a,b) : if x is between a and b, returns 1 else returns 0
clamp(x,a,b)   : see bound() above

ramp(value,a,b)        : returns 0 -> 1 as value goes from a to b
lerp(factor,a,b)       : returns a -> b as value goes from 0 to 1

cramp(value,a,b)        : clamp(ramp(value,a,b),0,1)
clerp(factor,a,b)       : clamp(lerp(factor,a,b),a,b)

elerp(x,a,b)         : ramp( 3*x*x - 2*x*x*x, a, b)
//elerp(factor,a,b)    : lerp(lerp(sind(clerp(factor,-90,90)),0.5,1.0),a,b)

noise(a,b,c) : { solid noise pattern (improved perlin noise) indexed with three numbers }
*/

static float ramp(const float x, const float a, const float b) {
    return (x - a) / (b - a);
}

// float lerp( float x, float a, float b )
// {
// 	return a + x * ( b - a );
// }

static float smoothstep(const float x) {
    return 3 * x * x - 2 * x * x * x;
}

bool CExpressionCalculator::Parse3ArgFunc(std::string_view &expr) {
    const std::vector<std::string> pFuncs = {"inrange", "clamp", "ramp", "lerp", "cramp", "clerp", "elerp", "noise"};
    if (size_t funcIndex; ParseStringList(expr, pFuncs, funcIndex) &&
                          ParseString(expr, "(") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ",") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ",") &&
                          ParseExpr(expr) &&
                          ParseString(expr, ")")) {
        const float f3 = m_stack.back();
        m_stack.pop_back();
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (funcIndex) {
            case 0: // inrange
                m_stack.emplace_back((f1 >= f2) && (f1 <= f3) ? 1.0f : 0.0f);
                break;
            case 1: // clamp
                m_stack.emplace_back(clamp(f1, f2, f3));
                break;
            case 2: // ramp
                m_stack.emplace_back(ramp(f1, f2, f3));
                break;
            case 3: // lerp
                m_stack.emplace_back(std::lerp(f1, f2, f3));
                break;
            case 4: // cramp
                m_stack.emplace_back(clamp(ramp(f1, f2, f3), 0, 1));
                break;
            case 5: // clerp
                m_stack.emplace_back(clamp(std::lerp(f1, f2, f3), f2, f3));
                break;
            case 6: // elerp
                m_stack.emplace_back(std::lerp(smoothstep(f1), f2, f3));
                break;
            case 7: // noise
                m_stack.emplace_back(ImprovedPerlinNoise(Vector(f1, f2, f3)));
                break;
            default:
                MdlError("Invalid 3 arg function");
        }
        return true;
    }
    return false;
}

//bool CExpressionCalculator::Parse4ArgFunc( const char *&expr );

/*
rescale (X,Xa,Xb,Ya,Yb) : lerp(ramp(X,Xa,Xb),Ya,Yb)
crescale(X,Xa,Xb,Ya,Yb) : clamp(rescale(X,Xa,Xb,Ya,Yb),Ya,Yb)
*/
static float rescale(const float x, const float a, const float b, const float c, const float d) {
    return lerp(ramp(x, a, b), c, d);
}

bool CExpressionCalculator::Parse5ArgFunc(std::string_view &expr) {
    const std::vector<std::string> pFuncs = {"rescale", "crescale"};

    if (size_t matchIndex; ParseStringList(expr, pFuncs, matchIndex) &&
        ParseString(expr, "(") &&
        ParseExpr(expr) &&
        ParseString(expr, ",") &&
        ParseExpr(expr) &&
        ParseString(expr, ",") &&
        ParseExpr(expr) &&
        ParseString(expr, ",") &&
        ParseExpr(expr) &&
        ParseString(expr, ",") &&
        ParseExpr(expr) &&
        ParseString(expr, ")")) {
        const float f5 = m_stack.back();
        m_stack.pop_back();
        const float f4 = m_stack.back();
        m_stack.pop_back();
        const float f3 = m_stack.back();
        m_stack.pop_back();
        const float f2 = m_stack.back();
        m_stack.pop_back();
        const float f1 = m_stack.back();
        m_stack.pop_back();
        switch (matchIndex) {
            case 0: // rescale
                m_stack.emplace_back(rescale(f1, f2, f3, f4, f5));
                break;
            case 1: // crescale
                m_stack.emplace_back(clamp(rescale(f1, f2, f3, f4, f5), f4, f5));
                break;
            default:
                MdlError("Invalid 5 arg function");
        }
        return true;
    }
    return false;
}