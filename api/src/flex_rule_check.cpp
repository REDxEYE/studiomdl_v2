#include "flex_rule_check.hpp"

#include <format>

#include "common/scriplib.h"
#include "studiomdl/apply_flex.hpp"
#include "studiomdl/studiomdl.h"

namespace {
    // Thrown by the tokenizer's error handler (the parser reports through ScriptTokenizer::Error)
    struct RuleParseError {
        std::string message;
    };

    std::string Trimmed(std::string message) {
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.pop_back();
        return message;
    }

    // The characters and sequences the tokenizer would treat as line ends or comments: the rest of the expression
    // would be dropped silently
    std::string FindForbidden(std::string_view text) {
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\n' || c == '\r')
                return "the expression must be on one line";
            if (c == ';' || c == '#')
                return std::format("'{}' starts a comment (position {})", c, i + 1);
            if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*'))
                return std::format("'{}{}' starts a comment (position {})", c, text[i + 1], i + 1);
        }
        return {};
    }

    // The operands and operators of a postfix rule must leave exactly one value
    std::string CheckArity(const s_flexrule_t &rule) {
        int depth = 0;
        for (int i = 0; i < rule.numops; ++i) {
            switch (rule.op[i].op) {
                case StudioOpcode::Const:
                case StudioOpcode::Fetch1:
                case StudioOpcode::Fetch2:
                    ++depth;
                    break;
                case StudioOpcode::Add:
                case StudioOpcode::Sub:
                case StudioOpcode::Mul:
                case StudioOpcode::Div:
                case StudioOpcode::Max:
                case StudioOpcode::Min:
                    if (depth < 2)
                        return "an operator or function is missing an operand";
                    --depth;
                    break;
                case StudioOpcode::Neg:
                    if (depth < 1)
                        return "a minus sign has no operand";
                    break;
                default:
                    return "unexpected token in the expression";
            }
        }
        if (depth != 1)
            return depth == 0 ? "the expression is empty" : "operands without an operator between them";
        return {};
    }
}

namespace {
    // `lookup` records unknown names in `notFound`
    FlexRuleCheck CheckWith(const std::string_view flexName, const int flexIndex, const std::string_view expression,
                            const FlexRuleLookup &lookup, const bool &notFound) {
        FlexRuleCheck result;
        auto fail = [&](smdl_status status, const std::string &what) {
            result.status = status;
            result.message = std::format("flex rule for '{}': {} (expression: {})", flexName, what, expression);
            return result;
        };

        if (expression.find_first_not_of(" \t") == std::string_view::npos)
            return fail(SMDL_ERROR_SYNTAX, "the expression is empty");
        if (const std::string forbidden = FindForbidden(expression); !forbidden.empty())
            return fail(SMDL_ERROR_SYNTAX, forbidden);

        ScriptTokenizer tokenizer;
        tokenizer.SetErrorHandler([](const std::string &message, const std::string &, int) {
            throw RuleParseError{message};
        });
        std::string buffer = "= ";
        buffer += expression;
        tokenizer.LoadMemory(buffer);

        s_flexrule_t rule{};
        try {
            ParseFlexRule(tokenizer, flexName, flexIndex, lookup, rule);
        }
        catch (const RuleParseError &error) {
            return fail(notFound ? SMDL_ERROR_NOT_FOUND : SMDL_ERROR_SYNTAX, Trimmed(error.message));
        }
        if (const std::string arity = CheckArity(rule); !arity.empty())
            return fail(SMDL_ERROR_SYNTAX, arity);
        return result;
    }
}

FlexRuleCheck CheckFlexRule(const FlexDesc &desc, const std::string_view flexName, const int flexIndex,
                            const std::string_view expression) {
    bool notFound = false;
    const FlexRuleLookup lookup{
        .flex = [&](std::string_view name) {
            const int index = desc.findFlex(name);
            notFound |= index < 0;
            return index;
        },
        .controller = [&](std::string_view name) {
            const int index = desc.findController(name);
            notFound |= index < 0;
            return index;
        },
    };
    return CheckWith(flexName, flexIndex, expression, lookup, notFound);
}

FlexRuleCheck CheckFlexRuleSyntax(const std::string_view flexName, const std::string_view expression) {
    const bool notFound = false;
    const FlexRuleLookup lookup{
        .flex = [](std::string_view) { return 0; },
        .controller = [](std::string_view) { return 0; },
    };
    return CheckWith(flexName, 0, expression, lookup, notFound);
}
