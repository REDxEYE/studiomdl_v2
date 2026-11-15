//====== Copyright c 1996-2008, Valve Corporation, All rights reserved. =======

#pragma once

#include <string>
#include <string_view>
#include <vector>

//-----------------------------------------------------------------------------
// Calculator Parsing class
// precedence order:
//		unary operators: + - ! func var
//		* / %
//		+ -
//		< > <= >=
//		== !=
//		&&
//		||
//		?:
//-----------------------------------------------------------------------------
class CExpressionCalculator {
public:
    CExpressionCalculator(const std::string_view expr = "") : m_expr(expr) {
    }

    CExpressionCalculator(const CExpressionCalculator &x) = default;

    CExpressionCalculator& operator=(const CExpressionCalculator &x) = default;

public:
    void SetExpression(const char* expr) {
        m_expr = expr;
    }

    void SetVariable(std::string_view var, float value);

    void SetVariable(size_t variableIndex, float value);

    bool Evaluate(float &value);

    // Builds a list of variable names from the expression
    bool BuildVariableListFromExpression();

    // Iterate over variables
    size_t VariableCount() const;

    std::string_view VariableName(size_t index);

private:
    bool ParseExpr(std::string_view &expr);

    bool ParseConditional(std::string_view &expr);

    bool ParseOr(std::string_view &expr);

    bool ParseAnd(std::string_view &expr);

    bool ParseEquality(std::string_view &expr);

    bool ParseLessGreater(std::string_view &expr);

    bool ParseAddSub(std::string_view &expr);

    bool ParseDivMul(std::string_view &expr);

    bool ParseUnary(std::string_view &expr);

    bool ParsePrimary(std::string_view &expr);

    bool Parse1ArgFunc(std::string_view &expr);

    bool Parse2ArgFunc(std::string_view &expr);

    bool Parse3ArgFunc(std::string_view &expr);

    //	bool Parse4ArgFunc	 ( const char *&expr );
    bool Parse5ArgFunc(std::string_view &expr);

    std::string m_expr;
    std::vector<std::string> m_varNames;
    std::vector<float> m_varValues;
    std::vector<float> m_stack;
    bool m_bIsBuildingArgumentList{};
};
