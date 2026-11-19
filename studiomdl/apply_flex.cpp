#include "studiomdl/apply_flex.hpp"

#include <cmath>
#include <format>

#include "common/scriplib.h"
#include "datamodel/idatamodel.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl_commands.h"
#include "studiomdl_errors.h"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/string_utils.hpp"

namespace {
    constexpr const char *kEyelidSuffix[kEyelidTypeCount] = {"lowerer", "neutral", "raiser"};

    std::string TooLong(std::string_view what, std::string_view name) {
        return std::format("{} \"{}\" is too long (max {} characters)\n", what, name, MAXSTUDIONAME - 1);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Flex rules
// ---------------------------------------------------------------------------------------------------------------

void ParseFlexRule(ScriptTokenizer &qc, const std::string_view flexName, const int flexdesc,
                   const FlexRuleLookup &lookup, s_flexrule_t &rule) {
    int precedence[32];
    auto prec = [&precedence](StudioOpcode op) -> int & { return precedence[std::to_underlying(op)]; };
    prec(StudioOpcode::Const) = 0;
    prec(StudioOpcode::Fetch1) = 0;
    prec(StudioOpcode::Fetch2) = 0;
    prec(StudioOpcode::Add) = 1;
    prec(StudioOpcode::Sub) = 1;
    prec(StudioOpcode::Mul) = 2;
    prec(StudioOpcode::Div) = 2;
    prec(StudioOpcode::Neg) = 4;
    prec(StudioOpcode::Exp) = 3;
    prec(StudioOpcode::Open) = 0; // only used in token parsing
    prec(StudioOpcode::Close) = 0;
    prec(StudioOpcode::Comma) = 0;
    prec(StudioOpcode::Max) = 5;
    prec(StudioOpcode::Min) = 5;

    // one more entry than MAX_OPS: the unary minus looks at the token after it
    s_flexop_t stream[MAX_OPS + 1] = {}; // operators have no operand: d must not carry stack garbage into the MDL
    int i = 0;
    s_flexop_t stack[MAX_OPS] = {};
    int j = 0;
    int k = 0;

    auto emit = [&](const StudioOpcode op) -> s_flexop_t & {
        if (i >= MAX_OPS) {
            qc.Error("expression {} too complicated\n", flexName);
        }
        stream[i].op = op;
        return stream[i++];
    };

    rule.flex = flexdesc;
    rule.numops = 0;

    // =
    qc.ExpectToken();

    // parse all the tokens
    bool linecontinue = false;
    while (linecontinue || qc.TokenAvailable()) {
        if (!qc.GetExprToken(linecontinue)) {
            qc.Error("unexpected end of file in flex rule for {}\n", flexName);
        }

        linecontinue = false;

        if (qc.token[0] == '\\') {
            if (!qc.GetToken(false) || qc.token[0] != '\\') {
                qc.Error("unknown expression token '\\{}\n", qc.token);
            }
            linecontinue = true;
        }
        else if (qc.token[0] == '(') {
            emit(StudioOpcode::Open);
        }
        else if (qc.token[0] == ')') {
            emit(StudioOpcode::Close);
        }
        else if (qc.token[0] == '+') {
            emit(StudioOpcode::Add);
        }
        else if (qc.token[0] == '-') {
            StudioOpcode op = StudioOpcode::Sub;
            if (i > 0) {
                switch (stream[i - 1].op) {
                    case StudioOpcode::Open:
                    case StudioOpcode::Add:
                    case StudioOpcode::Sub:
                    case StudioOpcode::Mul:
                    case StudioOpcode::Div:
                    case StudioOpcode::Comma:
                        // it's a unary if it's preceded by a "(+-*/,"?
                        op = StudioOpcode::Neg;
                        break;
                }
            }
            emit(op);
        }
        else if (qc.token[0] == '*') {
            emit(StudioOpcode::Mul);
        }
        else if (qc.token[0] == '/') {
            emit(StudioOpcode::Div);
        }
        else if (V_isdigit(qc.token[0])) {
            emit(StudioOpcode::Const).d.value = qc.TokenFloat();
        }
        else if (qc.token[0] == ',') {
            emit(StudioOpcode::Comma);
        }
        else if (vstd::iequals(qc.token, "max")) {
            emit(StudioOpcode::Max);
        }
        else if (vstd::iequals(qc.token, "min")) {
            emit(StudioOpcode::Min);
        }
        else {
            if (qc.token[0] == '%') {
                if (!qc.GetExprToken(false)) {
                    qc.Error("expected flex name after '%'\n");
                }

                k = lookup.flex(qc.token.view());
                if (k < 0) {
                    qc.Error("unknown flex {}\n", qc.token);
                }
                emit(StudioOpcode::Fetch2).d.index = k;
            }
            else {
                k = lookup.controller(qc.token.view());
                if (k < 0) {
                    qc.Error("unknown controller {}\n", qc.token);
                }
                emit(StudioOpcode::Fetch1).d.index = k;
            }
        }
    }

    j = 0;
    for (k = 0; k < i; k++) {
        if (j >= MAX_OPS) {
            qc.Error("expression {} too complicated\n", flexName);
        }
        switch (stream[k].op) {
            case StudioOpcode::Const:
            case StudioOpcode::Fetch1:
            case StudioOpcode::Fetch2:
                rule.op[rule.numops++] = stream[k];
                break;
            case StudioOpcode::Open:
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Close:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != StudioOpcode::Open) {
                    rule.op[rule.numops++] = stack[j - 1];
                    j--;
                }
                if (j == 0) {
                    qc.Error("unmatched closed parentheses\n");
                }
                if (j > 0)
                    j--;
                break;
            case StudioOpcode::Comma:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != StudioOpcode::Open) {
                    rule.op[rule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Add:
            case StudioOpcode::Sub:
            case StudioOpcode::Mul:
            case StudioOpcode::Div:
                // pop all operators off of the stack that have equal or higher precedence
                while (j > 0 && prec(stream[k].op) <= prec(stack[j - 1].op)) {
                    rule.op[rule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Neg:
                if (stream[k + 1].op == StudioOpcode::Const) {
                    // change sign of constant, skip op
                    stream[k + 1].d.value = -stream[k + 1].d.value;
                }
                else {
                    // push operator onto the stack
                    stack[j++] = stream[k];
                }
                break;
            case StudioOpcode::Max:
            case StudioOpcode::Min:
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
        }
        if (rule.numops >= MAX_OPS)
            qc.Error("expression for \"{}\" too complicated\n", flexName);
    }
    // pop all operators off of the stack
    while (j > 0) {
        if (stack[j - 1].op == StudioOpcode::Open) {
            qc.Error("unmatched open parentheses\n");
        }
        rule.op[rule.numops++] = stack[j - 1];
        j--;
        if (rule.numops >= MAX_OPS)
            qc.Error("expression for \"{}\" too complicated\n", flexName);
    }

    // reprocess the operands, eating commas for all functions
    int numCommas = 0;
    j = 0;
    for (k = 0; k < rule.numops; k++) {
        switch (rule.op[k].op) {
            case StudioOpcode::Max:
            case StudioOpcode::Min:
                if (j == 0 || rule.op[j - 1].op != StudioOpcode::Comma) {
                    qc.Error("missing comma\n");
                }
                // eat the comma operator
                numCommas--;
                rule.op[j - 1] = rule.op[k];
                break;
            case StudioOpcode::Comma:
                numCommas++;
                rule.op[j++] = rule.op[k];
                break;
            default:
                rule.op[j++] = rule.op[k];
                break;
        }
    }
    rule.numops = j;
    if (numCommas != 0) {
        qc.Error("too many comma's\n");
    }

    if (rule.numops > MAX_OPS) {
        qc.Error("expression {} too complicated\n", flexName);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Controllers, local variables, mouths
// ---------------------------------------------------------------------------------------------------------------

std::string ApplyFlexController(StudioMdlState &ctx, const std::string_view type, const std::string_view name,
                                const float min, const float max) {
    if (ctx.flex.flexcontrollers.size() >= ctx.flex.flexcontrollers.capacity()) {
        return std::format("Too many flex controllers, max {}\n", ctx.flex.flexcontrollers.capacity());
    }

    ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
        .name = name,
        .type = type,
        .min = min,
        .max = max
    });
    return {};
}

void ApplyLocalVar(StudioMdlState &ctx, const std::string_view name) {
    Add_Flexdesc(ctx, std::string(name).c_str());
}

void ApplyMouth(StudioMdlState &ctx, const size_t index, const std::string_view flex, const std::string_view bone,
                const Vector &forward) {
    s_mouth_t &mouth = ctx.flex.mouth[index];
    mouth.flexdesc = Add_Flexdesc(ctx, std::string(flex).c_str());
    mouth.bonename = bone;
    mouth.forward = forward;
}

// ---------------------------------------------------------------------------------------------------------------
// Flexes
// ---------------------------------------------------------------------------------------------------------------

std::string ApplyFlexKey(StudioMdlState &ctx, const FlexKeyParams &params, s_source_t *source, const int imodel) {
    if (ctx.flex.flexkeys.size() >= MAXSTUDIOFLEXKEYS) {
        return std::format("Too many flexes, max {}\n", MAXSTUDIOFLEXKEYS);
    }

    int flexdesc, flexpair;

    if (params.stereo) {
        flexdesc = Add_Flexdesc(ctx, std::format("{}R", params.name).c_str());
        flexpair = Add_Flexdesc(ctx, std::format("{}L", params.name).c_str());
    }
    else {
        flexdesc = Add_Flexdesc(ctx, std::string(params.name).c_str());
        flexpair = 0;
    }

    // initialize
    auto &new_flexkey = ctx.flex.flexkeys.emplace(s_flexkey_t{
        .flexdesc = flexdesc,
        .flexpair = flexpair,
        .imodel = imodel,
        .frame = params.frame,
        .target0 = 0.0,
        .target1 = params.position,
        .target2 = 10,
        .target3 = 11,
        .split = params.split,
        .decay = params.decay,
    });

    const auto current_count = ctx.flex.flexkeys.size() - 1;

    if (current_count > 1) {
        if (ctx.flex.flexkeys[current_count - 1].flexdesc == new_flexkey.flexdesc) {
            ctx.flex.flexkeys[current_count - 1].target2 = ctx.flex.flexkeys[current_count - 1].target1;
            ctx.flex.flexkeys[current_count - 1].target3 = new_flexkey.target1;
            new_flexkey.target0 = ctx.flex.flexkeys[current_count - 1].target1;
        }
    }

    // link to source
    new_flexkey.source = source;
    if (!source->m_Animations.empty()) {
        new_flexkey.animationname = source->m_Animations[0].animationname;
    }
    else {
        new_flexkey.animationname[0] = 0;
    }
    // this needs to be per model.
    return {};
}

std::string ApplyDefaultFlexKey(StudioMdlState &ctx, const FlexKeyParams &params, s_source_t *source,
                                const int imodel) {
    FlexKeyParams key = params;
    key.name = "default";
    std::string error = ApplyFlexKey(ctx, key, source, imodel);
    if (error.empty()) {
        ctx.flex.defaultflexkey = &ctx.flex.flexkeys.back();
    }
    return error;
}

// The option's name in the messages (they were written with __FUNCTION__ of the QC option parser)
static constexpr const char *kVertexCacheOption = "Option_VertexCacheAnimationFile";

void CheckVertexCacheAnimationAllowed(StudioMdlState &ctx) {
    if (!ctx.flex.flexkeys.empty()) {
        MdlError("{}: Flexes already defined.  vcafile can be only flex option in $model block\n",
                 kVertexCacheOption);
    }
}

void ApplyVertexCacheAnimation(StudioMdlState &ctx, const size_t modelIndex, s_source_t *vtaSource,
                               const std::string_view name, const std::span<const int> frames) {
    s_source_t *pSource = ctx.body.model[modelIndex]->source;

    {
        ctx.flex.flexkeys.emplace(s_flexkey_t{
            .flexdesc = Add_Flexdesc(ctx, "default"),
            .flexpair = 0,
            .source = vtaSource,
            .animationname = vtaSource->m_Animations[0].animationname,
            .imodel = static_cast<int>(modelIndex),
            .frame = 0,
            .target0 = 0.0,
            .target1 = 1.0,
            .target2 = 10,
            .target3 = 11,
            .split = 0,
            .decay = 0.0
        });
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        ctx.flex.flexkeys.emplace(s_flexkey_t{
            .flexdesc = Add_Flexdesc(ctx, std::format("f{}", i).c_str()),
            .flexpair = 0,
            .source = vtaSource,
            .animationname = vtaSource->m_Animations[0].animationname,
            .imodel = static_cast<int>(modelIndex),
            .frame = frames[i],
            .target0 = 0.0,
            .target1 = 1.0,
            .target2 = 10,
            .target3 = 11,
            .split = 0,
            .decay = 0.0,
        });
    }

    s_flexcontrollerremap_t &flexRemap = pSource->m_FlexControllerRemaps.emplace_back(s_flexcontrollerremap_t{
        .m_Name = std::string(name),
        .m_RemapType = Nway,
        .m_bIsStereo = false,
        .m_Index = -1, // Don't know this right now
        .m_LeftIndex = -1, // Don't know this right now
        .m_RightIndex = -1, // Don't know this right now
        .m_MultiIndex = -1, // Don't know this right now
        .m_EyesUpDownFlexController = -1,
        .m_BlinkController = -1,
    });

    for (size_t i = 0; i < frames.size(); ++i) {
        flexRemap.m_RawControls.emplace_back(std::format("f{}", i));
    }

    for (auto &m_RawControl: flexRemap.m_RawControls) {
        int nFlexKey = -1;
        for (int j = 0; j < ctx.flex.flexkeys.size(); ++j) {
            if (vstd::iequals(ctx.flex.flexdescs[ctx.flex.flexkeys[j].flexdesc].FACS, m_RawControl)) {
                nFlexKey = j;
                break;
            }
        }

        if (nFlexKey < 0) {
            MdlError("{} Cannot find flex to group \"{}\"\n", kVertexCacheOption, m_RawControl);
            pSource->m_FlexControllerRemaps.pop_back();
            return;
        }

        pSource->m_CombinationControls.emplace_back(m_RawControl);

        s_combinationrule_t &combinationRule = pSource->m_CombinationRules.emplace_back();
        combinationRule.m_nFlex = nFlexKey;
        combinationRule.m_Combination.push_back(nFlexKey - 1);
    }

    AddFlexControllers(ctx, pSource);

    AddBodyFlexRemaps(ctx, pSource);
}

// ---------------------------------------------------------------------------------------------------------------
// Eyeballs and eyelids
// ---------------------------------------------------------------------------------------------------------------

std::string ApplyEyeball(StudioMdlState &ctx, s_model_t *pmodel, const EyeballParams &params) {
    int i, j;

    if (params.name.size() >= MAXSTUDIONAME) {
        return TooLong("eyeball name", params.name);
    }
    if (pmodel->eyeballs.full()) {
        return std::format("Too many eyeballs, max {}\n", pmodel->eyeballs.capacity());
    }

    s_eyeball_t &eyeball = pmodel->eyeballs.emplace_back();

    // name
    eyeball.name = params.name;

    // bone name
    for (i = 0; i < pmodel->source->numbones; i++) {
        if (vstd::iequals(pmodel->source->localBone[i].name, params.bone)) {
            eyeball.bone = i;
            break;
        }
    }
    if (!ctx.options.createMakefile && i >= pmodel->source->numbones) {
        return std::format("unknown eyeball bone \"{}\"\n", params.bone);
    }

    Vector tmp = params.position;

    // mesh material
    auto mesh_material = UseTextureAsMaterial(ctx, LookupTexture(ctx, params.material));

    // diameter
    eyeball.radius = params.diameter / 2.0f;

    // Z angle offset
    eyeball.zoffset = tan(vstd::deg2rad(params.zAngleDegrees));

    // pupil scale
    eyeball.iris_scale = 1.0f / params.pupilScale;

    VectorCopy(tmp, eyeball.org);

    for (i = 0; i < pmodel->source->meshes.size(); i++) {
        j = pmodel->source->meshes[i].material;

        if (j == mesh_material) {
            eyeball.mesh = i; // FIXME: should this be pre-adjusted?
            break;
        }
    }

    if (!ctx.options.createMakefile && i >= pmodel->source->meshes.size()) {
        return std::format("can't find eyeball texture \"{}\" on model\n", params.material);
    }

    // translate eyeball into bone space
    VectorITransform(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.org);

    matrix3x4_t vtmp;
    AngleMatrix(ctx.script.defaultrotation, vtmp);

    VectorIRotate(Vector(0, 0, 1), vtmp, tmp);
    VectorIRotate(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.up);

    VectorIRotate(Vector(1, 0, 0), vtmp, tmp);
    VectorIRotate(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.forward);

    // these get overwritten by "eyelid" data
    eyeball.upperlidflexdesc = -1;
    eyeball.lowerlidflexdesc = -1;
    return {};
}

int AddEyelidFlexdesc(StudioMdlState &ctx, const std::string_view name) {
    if (ctx.flex.flexdescs.full()) {
        MdlError("Too many flex types, max {}\n", MAXSTUDIOFLEXDESC);
    }
    const int desc = static_cast<int>(ctx.flex.flexdescs.size());
    ctx.flex.flexdescs.emplace_back(name);
    return desc;
}

std::string ApplyEyelid(StudioMdlState &ctx, const int imodel, const EyelidParams &params, s_source_t *source) {
    const auto &[lowererframe, neutralframe, raiserframe] = params.frame;
    const auto &[lowerertarget, neutraltarget, raisertarget] = params.target;
    const auto &[lowererdesc, neutraldesc, raiserdesc] = params.desc;
    const int basedesc = params.baseDesc;
    const float split = params.split;

    if (ctx.flex.flexkeys.size() + 3 > MAXSTUDIOFLEXKEYS) {
        return std::format("Too many flexes, max {}\n", MAXSTUDIOFLEXKEYS);
    }

    auto &lower_flexkey = ctx.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = source,
        .imodel = imodel,
        .frame = lowererframe,
        .target0 = -11,
        .target1 = -10,
        .target2 = lowerertarget,
        .target3 = neutraltarget,
        .split = split,
        .decay = 0.0,
    });

    if (!source->m_Animations.empty()) {
        lower_flexkey.animationname = source->m_Animations[0].animationname;
    }
    else {
        lower_flexkey.animationname = "";
    }
    ctx.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = lower_flexkey.source,
        .animationname = lower_flexkey.animationname,
        .imodel = imodel,
        .frame = neutralframe,
        .target0 = lowerertarget,
        .target1 = neutraltarget,
        .target2 = neutraltarget,
        .target3 = raisertarget,
        .split = split,
        .decay = 0.0,
    });

    ctx.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = lower_flexkey.source,
        .animationname = lower_flexkey.animationname,
        .imodel = imodel,
        .frame = raiserframe,
        .target0 = neutraltarget,
        .target1 = raisertarget,
        .target2 = 10,
        .target3 = 11,
        .split = split,
        .decay = 0.0,
    });

    s_model_t *pmodel = ctx.body.model[imodel];
    for (int i = 0; i < pmodel->eyeballs.size(); i++) {
        s_eyeball_t *peyeball = &(pmodel->eyeballs[i]);

        if (!params.eyeball.empty()) {
            if (!vstd::iequals(peyeball->name, params.eyeball))
                continue;
        }

        if (fabs(lowerertarget) > peyeball->radius) {
            return std::format("Eyelid \"{}\" lowerer out of range (+-{:.1f})\n", params.type, peyeball->radius);
        }
        if (fabs(neutraltarget) > peyeball->radius) {
            return std::format("Eyelid \"{}\" neutral out of range (+-{:.1f})\n", params.type, peyeball->radius);
        }
        if (fabs(raisertarget) > peyeball->radius) {
            return std::format("Eyelid \"{}\" raiser  out of range (+-{:.1f})\n", params.type, peyeball->radius);
        }

        switch (params.kind) {
            case EyelidKind::Upper:
                peyeball->upperlidflexdesc = basedesc;
                peyeball->upperflexdesc[0] = lowererdesc;
                peyeball->uppertarget[0] = lowerertarget;
                peyeball->upperflexdesc[1] = neutraldesc;
                peyeball->uppertarget[1] = neutraltarget;
                peyeball->upperflexdesc[2] = raiserdesc;
                peyeball->uppertarget[2] = raisertarget;
                break;
            case EyelidKind::Lower:
                peyeball->lowerlidflexdesc = basedesc;
                peyeball->lowerflexdesc[0] = lowererdesc;
                peyeball->lowertarget[0] = lowerertarget;
                peyeball->lowerflexdesc[1] = neutraldesc;
                peyeball->lowertarget[1] = neutraltarget;
                peyeball->lowerflexdesc[2] = raiserdesc;
                peyeball->lowertarget[2] = raisertarget;
                break;
            case EyelidKind::Other:
                break;
        }
    }
    return {};
}

const s_sourceanim_t *FindNewStyleVertexAnim(const s_source_t *source, const std::string_view name) {
    for (const auto &animation: source->m_Animations) {
        if (!animation.newStyleVertexAnimations)
            continue;

        if (vstd::iequals(name, animation.animationname))
            return &animation;
    }

    return nullptr;
}

std::string ApplyDmxEyelid(StudioMdlState &ctx, const int imodel, const DmxEyelidParams &params, s_source_t *source) {
    enum RightLeftType_t {
        kLeft = 0,
        kRight = 1,
        kRightLeftTypeCount = 2
    };

    struct EyelidData_t {
        int m_nFlexDesc[kRightLeftTypeCount];
        const s_sourceanim_t *m_pSourceAnim;
        float m_flTarget;
        const char *m_pszSuffix;
    };

    EyelidData_t eyelidData[3] =
    {
        {{-1, -1}, nullptr, params.target[kLowerer], kEyelidSuffix[kLowerer]},
        {{-1, -1}, nullptr, params.target[kNeutral], kEyelidSuffix[kNeutral]},
        {{-1, -1}, nullptr, params.target[kRaiser], kEyelidSuffix[kRaiser]}
    };

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        eyelidData[i].m_pSourceAnim = FindNewStyleVertexAnim(source, params.animation[i]);
        if (eyelidData[i].m_pSourceAnim == nullptr) {
            return std::format("dmxeyelid: no DMX vertex animation \"{}\" for {} in source \"{}\"\n",
                               params.animation[i], eyelidData[i].m_pszSuffix, source->filename);
        }
    }

    if (ctx.flex.flexkeys.size() + 3 > MAXSTUDIOFLEXKEYS) {
        return std::format("Too many flexes, max {}\n", MAXSTUDIOFLEXKEYS);
    }

    const char *pszType = params.upper ? "upper" : "lower";

    // Add a flexdesc for <type>_right & <type>_left
    // Where <type> is "upper" or "lower"
    int nRightLeftBaseDesc[kRightLeftTypeCount] = {-1, -1};

    const std::string sRightBaseDesc = std::string(pszType) + "_right";
    nRightLeftBaseDesc[kRight] = Add_Flexdesc(ctx, sRightBaseDesc.c_str());

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        const std::string sRightLocalDesc = sRightBaseDesc + "_" + eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kRight] = Add_Flexdesc(ctx, sRightLocalDesc.c_str());
    }

    const std::string sLeftBaseDesc = std::string(pszType) + "_left";
    nRightLeftBaseDesc[kLeft] = Add_Flexdesc(ctx, sLeftBaseDesc.c_str());

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        const std::string sLeftLocalDesc = sLeftBaseDesc + "_" + eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kLeft] = Add_Flexdesc(ctx, sLeftLocalDesc.c_str());
    }

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        s_flexkey_t *pFlexKey = &ctx.flex.flexkeys.emplace_back(s_flexkey_t{
            .flexdesc = nRightLeftBaseDesc[kLeft],
            .flexpair = nRightLeftBaseDesc[kRight],
            .source = source,
            .animationname = eyelidData[i].m_pSourceAnim->animationname,
            .imodel = imodel,
            .frame = 0,
            .split = 0.0f,
            .decay = 1.0
        });
        switch (i) {
            case kLowerer:
                pFlexKey->target0 = -11;
                pFlexKey->target1 = -10;
                pFlexKey->target2 = eyelidData[kLowerer].m_flTarget;
                pFlexKey->target3 = eyelidData[kNeutral].m_flTarget;
                break;
            case kNeutral:
                pFlexKey->target0 = eyelidData[kLowerer].m_flTarget;
                pFlexKey->target1 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target2 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target3 = eyelidData[kRaiser].m_flTarget;
                break;
            case kRaiser:
                pFlexKey->target0 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target1 = eyelidData[kRaiser].m_flTarget;
                pFlexKey->target2 = 10;
                pFlexKey->target3 = 11;
                break;
            default: {
                throw std::runtime_error("Invalid eyelid type");
            };
        }
    }

    bool bRightOk = false;
    bool bLeftOk = false;

    s_model_t *pModel = ctx.body.model[imodel];
    for (int i = 0; i < pModel->eyeballs.size(); ++i) {
        s_eyeball_t *pEyeball = &(pModel->eyeballs[i]);
        if (!pEyeball)
            continue;

        RightLeftType_t nRightLeftIndex = kRight;
        if (vstd::iequals(params.rightEyeball, pEyeball->name)) {
            nRightLeftIndex = kRight;
            bRightOk = true;
        }
        else if (vstd::iequals(params.leftEyeball, pEyeball->name)) {
            nRightLeftIndex = kLeft;
            bLeftOk = true;
        }
        else {
            MdlWarning("Unknown Eyeball: {}\n", pEyeball->name);
            continue;
        }

        for (int j = 0; j < kEyelidTypeCount; ++j) {
            if (fabs(eyelidData[j].m_flTarget) > pEyeball->radius) {
                return std::format("Eyelid \"{}\" {} {:.1f} out of range (+-{:.1f})\n", pszType,
                                   eyelidData[j].m_pszSuffix, eyelidData[j].m_flTarget, pEyeball->radius);
            }
        }

        if (params.upper) {
            pEyeball->upperlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
            for (int j = 0; j < kEyelidTypeCount; ++j) {
                pEyeball->upperflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                pEyeball->uppertarget[j] = eyelidData[j].m_flTarget;
            }
        }
        else {
            pEyeball->lowerlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
            for (int j = 0; j < kEyelidTypeCount; ++j) {
                pEyeball->lowerflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                pEyeball->lowertarget[j] = eyelidData[j].m_flTarget;
            }
        }
    }

    if (!bRightOk) {
        return std::format("Could not find right eye \"{}\"\n", params.rightEyeball);
    }

    if (!bLeftOk) {
        return std::format("Could not find left eye \"{}\"\n", params.leftEyeball);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Source modifiers
// ---------------------------------------------------------------------------------------------------------------

std::string ApplySphereNormals(StudioMdlState &ctx, s_source_t *psource, const std::string_view material,
                               const Vector &pos) {
    const auto meshMaterial = UseTextureAsMaterial(ctx, LookupTexture(ctx, material));

    for (const auto &mesh: psource->meshes) {
        if (mesh.material == meshMaterial) {
            s_vertexinfo_t *vertex = &psource->vertex[mesh.vertexoffset];

            for (int k = 0; k < mesh.numvertices; k++) {
                Vector n = vertex[k].position - pos;
                VectorNormalize(n);
                if (DotProduct(n, vertex[k].normal) < 0.0f) {
                    vertex[k].normal = -1 * n;
                }
                else {
                    vertex[k].normal = n;
                }
            }
            return {};
        }
    }

    return std::format("can't find spherenormal texture \"{}\" on model\n", material);
}

void ApplyNoAutoDmxRules(StudioMdlState &ctx, s_source_t *pSource) {
    // zero out the automatic flex controllers
    ctx.flex.flexcontrollers.clear();
    pSource->bNoAutoDMXRules = true;
}

// ---------------------------------------------------------------------------------------------------------------
// $boneflexdriver
// ---------------------------------------------------------------------------------------------------------------

std::string ApplyBoneFlexDriver(StudioMdlState &ctx, const std::string_view bone, const int component,
                                const std::string_view controller, const float min, const float max) {
    // Find or create the DmeBoneFlexDriverList
    CDmeBoneFlexDriverList *pDmeBoneFlexDriverList = GetElement<CDmeBoneFlexDriverList>(
        ctx.flex.hDmeBoneFlexDriverList);
    if (!pDmeBoneFlexDriverList) {
        pDmeBoneFlexDriverList = CreateElement<CDmeBoneFlexDriverList>("boneDriverFlexList", DMFILEID_INVALID);
        if (pDmeBoneFlexDriverList) {
            ctx.flex.hDmeBoneFlexDriverList = pDmeBoneFlexDriverList->GetHandle();
        }
    }

    if (!pDmeBoneFlexDriverList) {
        return "$boneflexdriver: Couldn't find or create DmeBoneDriverFlexList\n";
    }

    CDmeBoneFlexDriver *pDmeBoneFlexDriver = pDmeBoneFlexDriverList->FindOrCreateBoneFlexDriver(std::string(bone).c_str());
    if (!pDmeBoneFlexDriver) {
        return std::format("$boneflexdriver: Couldn't find or create DmeBoneFlexDriver for bone \"{}\"\n", bone);
    }

    CDmeBoneFlexDriverControl *pDmeBoneFlexDriverControl = pDmeBoneFlexDriver->FindOrCreateControl(
        std::string(controller).c_str());
    if (!pDmeBoneFlexDriverControl) {
        return std::format("$boneflexdriver: Couldn't find or create DmeBoneFlexDriverControl for bone \"{}\"\n",
                           controller);
    }

    pDmeBoneFlexDriverControl->m_nBoneComponent = component;
    pDmeBoneFlexDriverControl->m_flMin = min;
    pDmeBoneFlexDriverControl->m_flMax = max;
    return {};
}
