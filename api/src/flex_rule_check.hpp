// Call-time check of a flex rule expression (Flex part): runs studiomdl's own rule parser (ParseFlexRule) over a
// memory tokenizer with the names the model's description knows.
#pragma once

#include <string>
#include <string_view>

#include "desc_flex.hpp"

struct FlexRuleCheck {
    smdl_status status = SMDL_OK; // SMDL_ERROR_SYNTAX / SMDL_ERROR_NOT_FOUND on failure
    std::string message;
};

// flexName/flexIndex: the rule's flex (as the description stores it). Expression: the right-hand side of the rule.
FlexRuleCheck CheckFlexRule(const FlexDesc &desc, std::string_view flexName, int flexIndex,
                            std::string_view expression);

// Syntax-only check (every flex and controller name resolves): for expressions whose names the compile resolves, e.g.
// the flex rules of a mesh's combination operator. SMDL_ERROR_SYNTAX on failure.
FlexRuleCheck CheckFlexRuleSyntax(std::string_view flexName, std::string_view expression);
