/*
 * Copyright 2021 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/ir/SkSLFunctionDeclaration.h"

#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkSLDefines.h"
#include "include/private/SkSLLayout.h"
#include "include/private/SkSLModifiers.h"
#include "include/private/SkSLProgramKind.h"
#include "include/private/SkStringView.h"
#include "include/private/SkTHash.h"
#include "include/sksl/SkSLErrorReporter.h"
#include "include/sksl/SkSLPosition.h"
#include "src/sksl/SkSLBuiltinTypes.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/SkSLModifiersPool.h"
#include "src/sksl/SkSLProgramSettings.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLVariable.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace SkSL {

static IntrinsicKind identify_intrinsic(std::string_view functionName) {
    #define SKSL_INTRINSIC(name) {#name, k_##name##_IntrinsicKind},
    static const auto* kAllIntrinsics = new SkTHashMap<std::string_view, IntrinsicKind>{
        SKSL_INTRINSIC_LIST
    };
    #undef SKSL_INTRINSIC

    if (skstd::starts_with(functionName, '$')) {
        functionName.remove_prefix(1);
    }

    IntrinsicKind* kind = kAllIntrinsics->find(functionName);
    return kind ? *kind : kNotIntrinsic;
}

static bool check_modifiers(const Context& context,
                            Position pos,
                            const Modifiers& modifiers) {
    const int permitted = Modifiers::kHasSideEffects_Flag |
                          Modifiers::kInline_Flag |
                          Modifiers::kNoInline_Flag |
                          (context.fConfig->fIsBuiltinCode ? Modifiers::kES3_Flag : 0);
    modifiers.checkPermitted(context, pos, permitted, /*permittedLayoutFlags=*/0);
    if ((modifiers.fFlags & Modifiers::kInline_Flag) &&
        (modifiers.fFlags & Modifiers::kNoInline_Flag)) {
        context.fErrors->error(pos, "functions cannot be both 'inline' and 'noinline'");
        return false;
    }
    return true;
}

static bool check_return_type(const Context& context, Position pos, const Type& returnType) {
    ErrorReporter& errors = *context.fErrors;
    if (returnType.isArray()) {
        errors.error(pos, "functions may not return type '" + returnType.displayName() + "'");
        return false;
    }
    if (context.fConfig->strictES2Mode() && returnType.isOrContainsArray()) {
        errors.error(pos, "functions may not return structs containing arrays");
        return false;
    }
    if (!context.fConfig->fIsBuiltinCode && returnType.componentType().isOpaque()) {
        errors.error(pos, "functions may not return opaque type '" + returnType.displayName() +
                "'");
        return false;
    }
    return true;
}

static bool check_parameters(const Context& context,
                             std::vector<std::unique_ptr<Variable>>& parameters,
                             bool isMain) {
    auto typeIsValidForColor = [&](const Type& type) {
        return type.matches(*context.fTypes.fHalf4) || type.matches(*context.fTypes.fFloat4);
    };

    // The first color parameter passed to main() is the input color; the second is the dest color.
    static constexpr int kBuiltinColorIDs[] = {SK_INPUT_COLOR_BUILTIN, SK_DEST_COLOR_BUILTIN};
    unsigned int builtinColorIndex = 0;

    // Check modifiers on each function parameter.
    for (auto& param : parameters) {
        const Type& type = param->type();
        int permittedFlags = Modifiers::kConst_Flag | Modifiers::kIn_Flag;
        if (!type.isOpaque()) {
            permittedFlags |= Modifiers::kOut_Flag;
        }
        if (type.typeKind() == Type::TypeKind::kTexture) {
            permittedFlags |= Modifiers::kReadOnly_Flag | Modifiers::kWriteOnly_Flag;
        }
        param->modifiers().checkPermitted(context,
                                          param->modifiersPosition(),
                                          permittedFlags,
                                          /*permittedLayoutFlags=*/0);
        // Only the (builtin) declarations of 'sample' are allowed to have shader/colorFilter or FP
        // parameters. You can pass other opaque types to functions safely; this restriction is
        // specific to "child" objects.
        if (type.isEffectChild() && !context.fConfig->fIsBuiltinCode) {
            context.fErrors->error(param->fPosition, "parameters of type '" + type.displayName() +
                                                     "' not allowed");
            return false;
        }

        Modifiers m = param->modifiers();
        bool modifiersChanged = false;

        // The `in` modifier on function parameters is implicit, so we can replace `in float x` with
        // `float x`. This prevents any ambiguity when matching a function by its param types.
        if (Modifiers::kIn_Flag == (m.fFlags & (Modifiers::kOut_Flag | Modifiers::kIn_Flag))) {
            m.fFlags &= ~(Modifiers::kOut_Flag | Modifiers::kIn_Flag);
            modifiersChanged = true;
        }

        if (isMain) {
            if (ProgramConfig::IsRuntimeEffect(context.fConfig->fKind) &&
                context.fConfig->fKind != ProgramKind::kMeshFragment &&
                context.fConfig->fKind != ProgramKind::kMeshVertex) {
                // We verify that the signature is fully correct later. For now, if this is a
                // runtime effect of any flavor, a float2 param is supposed to be the coords, and a
                // half4/float parameter is supposed to be the input or destination color:
                if (type.matches(*context.fTypes.fFloat2)) {
                    m.fLayout.fBuiltin = SK_MAIN_COORDS_BUILTIN;
                    modifiersChanged = true;
                } else if (typeIsValidForColor(type) &&
                           builtinColorIndex < std::size(kBuiltinColorIDs)) {
                    m.fLayout.fBuiltin = kBuiltinColorIDs[builtinColorIndex++];
                    modifiersChanged = true;
                }
            } else if (ProgramConfig::IsFragment(context.fConfig->fKind)) {
                // For testing purposes, we have .sksl inputs that are treated as both runtime
                // effects and fragment shaders. To make that work, fragment shaders are allowed to
                // have a coords parameter.
                if (type.matches(*context.fTypes.fFloat2)) {
                    m.fLayout.fBuiltin = SK_MAIN_COORDS_BUILTIN;
                    modifiersChanged = true;
                }
            }
        }

        if (modifiersChanged) {
            param->setModifiers(context.fModifiersPool->add(m));
        }
    }
    return true;
}

static bool check_main_signature(const Context& context, Position pos, const Type& returnType,
                                 std::vector<std::unique_ptr<Variable>>& parameters) {
    ErrorReporter& errors = *context.fErrors;
    ProgramKind kind = context.fConfig->fKind;

    auto typeIsValidForColor = [&](const Type& type) {
        return type.matches(*context.fTypes.fHalf4) || type.matches(*context.fTypes.fFloat4);
    };

    auto typeIsValidForAttributes = [&](const Type& type) {
        return type.isStruct() && type.name() == "Attributes";
    };

    auto typeIsValidForVaryings = [&](const Type& type) {
        return type.isStruct() && type.name() == "Varyings";
    };

    auto paramIsCoords = [&](int idx) {
        const Variable& p = *parameters[idx];
        return p.type().matches(*context.fTypes.fFloat2) &&
               p.modifiers().fFlags == 0 &&
               p.modifiers().fLayout.fBuiltin == SK_MAIN_COORDS_BUILTIN;
    };

    auto paramIsBuiltinColor = [&](int idx, int builtinID) {
        const Variable& p = *parameters[idx];
        return typeIsValidForColor(p.type()) &&
               p.modifiers().fFlags == 0 &&
               p.modifiers().fLayout.fBuiltin == builtinID;
    };

    auto paramIsInAttributes = [&](int idx) {
        const Variable& p = *parameters[idx];
        return typeIsValidForAttributes(p.type()) && p.modifiers().fFlags == 0;
    };

    auto paramIsOutVaryings = [&](int idx) {
        const Variable& p = *parameters[idx];
        return typeIsValidForVaryings(p.type()) && p.modifiers().fFlags == Modifiers::kOut_Flag;
    };

    auto paramIsInVaryings = [&](int idx) {
        const Variable& p = *parameters[idx];
        return typeIsValidForVaryings(p.type()) && p.modifiers().fFlags == 0;
    };

    auto paramIsOutColor = [&](int idx) {
        const Variable& p = *parameters[idx];
        return typeIsValidForColor(p.type()) && p.modifiers().fFlags == Modifiers::kOut_Flag;
    };

    auto paramIsInputColor = [&](int n) { return paramIsBuiltinColor(n, SK_INPUT_COLOR_BUILTIN); };
    auto paramIsDestColor  = [&](int n) { return paramIsBuiltinColor(n, SK_DEST_COLOR_BUILTIN); };

    switch (kind) {
        case ProgramKind::kRuntimeColorFilter: {
            // (half4|float4) main(half4|float4)
            if (!typeIsValidForColor(returnType)) {
                errors.error(pos, "'main' must return: 'vec4', 'float4', or 'half4'");
                return false;
            }
            bool validParams = (parameters.size() == 1 && paramIsInputColor(0));
            if (!validParams) {
                errors.error(pos, "'main' parameter must be 'vec4', 'float4', or 'half4'");
                return false;
            }
            break;
        }
        case ProgramKind::kRuntimeShader:
        case ProgramKind::kPrivateRuntimeShader: {
            // (half4|float4) main(float2)  -or-  (half4|float4) main(float2, half4|float4)
            if (!typeIsValidForColor(returnType)) {
                errors.error(pos, "'main' must return: 'vec4', 'float4', or 'half4'");
                return false;
            }
            bool validParams =
                    (parameters.size() == 1 && paramIsCoords(0)) ||
                    (parameters.size() == 2 && paramIsCoords(0) && paramIsInputColor(1));
            if (!validParams) {
                errors.error(pos, "'main' parameters must be (float2, (vec4|float4|half4)?)");
                return false;
            }
            break;
        }
        case ProgramKind::kRuntimeBlender: {
            // (half4|float4) main(half4|float4, half4|float4)
            if (!typeIsValidForColor(returnType)) {
                errors.error(pos, "'main' must return: 'vec4', 'float4', or 'half4'");
                return false;
            }
            if (!(parameters.size() == 2 &&
                  paramIsInputColor(0) &&
                  paramIsDestColor(1))) {
                errors.error(pos, "'main' parameters must be (vec4|float4|half4, "
                        "vec4|float4|half4)");
                return false;
            }
            break;
        }
        case ProgramKind::kMeshVertex: {
            // float2 main(Attributes, out Varyings)
            if (!returnType.matches(*context.fTypes.fFloat2)) {
                errors.error(pos, "'main' must return: 'vec2' or 'float2'");
                return false;
            }
            if (!(parameters.size() == 2 && paramIsInAttributes(0) && paramIsOutVaryings(1))) {
                errors.error(pos, "'main' parameters must be (Attributes, out Varyings");
                return false;
            }
            break;
        }
        case ProgramKind::kMeshFragment: {
            // float2 main(Varyings) -or- float2 main(Varyings, out half4|float4]) -or-
            // void main(Varyings) -or- void main(Varyings, out half4|float4])
            if (!returnType.matches(*context.fTypes.fFloat2) &&
                !returnType.matches(*context.fTypes.fVoid)) {
                errors.error(pos, "'main' must return: 'vec2', 'float2', 'or' 'void'");
                return false;
            }
            if (!((parameters.size() == 1 && paramIsInVaryings(0)) ||
                  (parameters.size() == 2 && paramIsInVaryings(0) && paramIsOutColor(1)))) {
                errors.error(pos, "'main' parameters must be (Varyings, (out (half4|float4))?)");
                return false;
            }
            break;
        }
        case ProgramKind::kGeneric:
            // No rules apply here
            break;
        case ProgramKind::kFragment:
        case ProgramKind::kGraphiteFragment: {
            bool validParams = (parameters.size() == 0) ||
                               (parameters.size() == 1 && paramIsCoords(0));
            if (!validParams) {
                errors.error(pos, "shader 'main' must be main() or main(float2)");
                return false;
            }
            break;
        }
        case ProgramKind::kVertex:
        case ProgramKind::kGraphiteVertex:
        case ProgramKind::kCompute:
            if (!returnType.matches(*context.fTypes.fVoid)) {
                errors.error(pos, "'main' must return 'void'");
                return false;
            }
            if (parameters.size()) {
                errors.error(pos, "shader 'main' must have zero parameters");
                return false;
            }
            break;
    }
    return true;
}

/**
 * Given a concrete type (`float3`) and a generic type (`$genType`), returns the index of the
 * concrete type within the generic type's typelist. Returns -1 if there is no match.
 */
static int find_generic_index(const Type& concreteType,
                              const Type& genericType,
                              bool allowNarrowing) {
    SkSpan<const Type* const> genericTypes = genericType.coercibleTypes();
    for (size_t index = 0; index < genericTypes.size(); ++index) {
        if (concreteType.canCoerceTo(*genericTypes[index], allowNarrowing)) {
            return index;
        }
    }
    return -1;
}

/** Returns true if the types match, or if `concreteType` can be found in `maybeGenericType`. */
static bool type_generically_matches(const Type& concreteType, const Type& maybeGenericType) {
    return maybeGenericType.isGeneric()
                ? find_generic_index(concreteType, maybeGenericType, /*allowNarrowing=*/false) != -1
                : concreteType.matches(maybeGenericType);
}

/**
 * Checks a parameter list (params) against the parameters of a function that was declared earlier
 * (otherParams). Returns true if they match, even if the parameters in `otherParams` contain
 * generic types.
 */
static bool parameters_match(const std::vector<std::unique_ptr<Variable>>& params,
                             const std::vector<const Variable*>& otherParams) {
    // If the param lists are different lengths, they're definitely not a match.
    if (params.size() != otherParams.size()) {
        return false;
    }

    // Figure out a consistent generic index (or bail if we find a contradiction).
    int genericIndex = -1;
    for (size_t i = 0; i < params.size(); ++i) {
        const Type* paramType = &params[i]->type();
        const Type* otherParamType = &otherParams[i]->type();

        if (otherParamType->isGeneric()) {
            int genericIndexForThisParam = find_generic_index(*paramType, *otherParamType,
                                                              /*allowNarrowing=*/false);
            if (genericIndexForThisParam == -1) {
                // The type wasn't a match for this generic at all; these params can't be a match.
                return false;
            }
            if (genericIndex != -1 && genericIndex != genericIndexForThisParam) {
                // The generic index mismatches from what we determined on a previous parameter.
                return false;
            }
            genericIndex = genericIndexForThisParam;
        }
    }

    // Now that we've determined a generic index (if we needed one), do a parameter check.
    for (size_t i = 0; i < params.size(); i++) {
        const Type* paramType = &params[i]->type();
        const Type* otherParamType = &otherParams[i]->type();

        // Make generic types concrete.
        if (otherParamType->isGeneric()) {
            SkASSERT(genericIndex != -1);
            SkASSERT(genericIndex < (int)otherParamType->coercibleTypes().size());
            otherParamType = otherParamType->coercibleTypes()[genericIndex];
        }
        // Detect type mismatches.
        if (!paramType->matches(*otherParamType)) {
            return false;
        }
    }
    return true;
}

/**
 * Checks for a previously existing declaration of this function, reporting errors if there is an
 * incompatible symbol. Returns true and sets outExistingDecl to point to the existing declaration
 * (or null if none) on success, returns false on error.
 */
static bool find_existing_declaration(const Context& context,
                                      SymbolTable& symbols,
                                      Position pos,
                                      std::string_view name,
                                      std::vector<std::unique_ptr<Variable>>& parameters,
                                      Position returnTypePos,
                                      const Type* returnType,
                                      const FunctionDeclaration** outExistingDecl) {
    ErrorReporter& errors = *context.fErrors;
    const Symbol* entry = symbols[name];
    *outExistingDecl = nullptr;
    if (entry) {
        if (!entry->is<FunctionDeclaration>()) {
            errors.error(pos, "symbol '" + std::string(name) + "' was already defined");
            return false;
        }
        for (const FunctionDeclaration* other = &entry->as<FunctionDeclaration>();
             other; other = other->nextOverload()) {
            SkASSERT(name == other->name());
            if (!parameters_match(parameters, other->parameters())) {
                continue;
            }
            if (!type_generically_matches(*returnType, other->returnType())) {
                std::vector<const Variable*> paramPtrs;
                paramPtrs.reserve(parameters.size());
                for (std::unique_ptr<Variable>& param : parameters) {
                    paramPtrs.push_back(param.get());
                }
                FunctionDeclaration invalidDecl(pos,
                                                &other->modifiers(),
                                                name,
                                                std::move(paramPtrs),
                                                returnType,
                                                context.fConfig->fIsBuiltinCode);
                errors.error(returnTypePos,
                             "functions '" + invalidDecl.description() + "' and '" +
                             other->description() + "' differ only in return type");
                return false;
            }
            for (size_t i = 0; i < parameters.size(); i++) {
                if (parameters[i]->modifiers() != other->parameters()[i]->modifiers()) {
                    errors.error(parameters[i]->fPosition, "modifiers on parameter " +
                            std::to_string(i + 1) + " differ between declaration and definition");
                    return false;
                }
            }
            if (other->definition() || other->isBuiltin()) {
                errors.error(pos, "duplicate definition of " + other->description());
                return false;
            }
            *outExistingDecl = other;
            break;
        }
    }
    return true;
}

FunctionDeclaration::FunctionDeclaration(Position pos,
                                         const Modifiers* modifiers,
                                         std::string_view name,
                                         std::vector<const Variable*> parameters,
                                         const Type* returnType,
                                         bool builtin)
        : INHERITED(pos, kSymbolKind, name, /*type=*/nullptr)
        , fDefinition(nullptr)
        , fModifiers(modifiers)
        , fParameters(std::move(parameters))
        , fReturnType(returnType)
        , fBuiltin(builtin)
        , fIsMain(name == "main")
        , fIntrinsicKind(builtin ? identify_intrinsic(name) : kNotIntrinsic) {
    // None of the parameters are allowed to be be null.
    SkASSERT(std::count(fParameters.begin(), fParameters.end(), nullptr) == 0);
}

const FunctionDeclaration* FunctionDeclaration::Convert(
        const Context& context,
        SymbolTable& symbols,
        Position pos,
        Position modifiersPosition,
        const Modifiers* modifiers,
        std::string_view name,
        std::vector<std::unique_ptr<Variable>> parameters,
        Position returnTypePos,
        const Type* returnType) {
    bool isMain = (name == "main");

    const FunctionDeclaration* decl = nullptr;
    if (!check_modifiers(context, modifiersPosition, *modifiers) ||
        !check_return_type(context, returnTypePos, *returnType) ||
        !check_parameters(context, parameters, isMain) ||
        (isMain && !check_main_signature(context, pos, *returnType, parameters)) ||
        !find_existing_declaration(context, symbols, pos, name, parameters, returnTypePos,
                                   returnType, &decl)) {
        return nullptr;
    }
    std::vector<const Variable*> finalParameters;
    finalParameters.reserve(parameters.size());
    for (std::unique_ptr<Variable>& param : parameters) {
        finalParameters.push_back(symbols.takeOwnershipOfSymbol(std::move(param)));
    }
    if (decl) {
        return decl;
    }
    auto result = std::make_unique<FunctionDeclaration>(pos,
                                                        modifiers,
                                                        name,
                                                        std::move(finalParameters),
                                                        returnType,
                                                        context.fConfig->fIsBuiltinCode);
    return symbols.add(std::move(result));
}

std::string FunctionDeclaration::mangledName() const {
    if ((this->isBuiltin() && !this->definition()) || this->isMain()) {
        // Builtins without a definition (like `sin` or `sqrt`) must use their real names.
        return std::string(this->name());
    }
    // Built-in functions can have a $ prefix, which will fail to compile in GLSL. Remove the
    // $ and add a unique mangling specifier, so user code can't conflict with the name.
    std::string_view name = this->name();
    const char* builtinMarker = "";
    if (skstd::starts_with(name, '$')) {
        name.remove_prefix(1);
        builtinMarker = "Q";  // a unique, otherwise-unused mangle character
    }
    // GLSL forbids two underscores in a row; add an extra character if necessary to avoid this.
    const char* splitter = skstd::ends_with(name, '_') ? "x_" : "_";
    // Rename function to `funcname_returntypeparamtypes`.
    std::string result = std::string(name) + splitter + builtinMarker +
                         this->returnType().abbreviatedName();
    for (const Variable* p : this->parameters()) {
        result += p->type().abbreviatedName();
    }
    return result;
}

std::string FunctionDeclaration::description() const {
    std::string result = this->returnType().displayName() + " " + std::string(this->name()) + "(";
    std::string separator;
    for (const Variable* p : this->parameters()) {
        result += separator;
        separator = ", ";
        result += p->type().displayName();
        result += " ";
        result += p->name();
    }
    result += ")";
    return result;
}

bool FunctionDeclaration::matches(const FunctionDeclaration& f) const {
    if (this->name() != f.name()) {
        return false;
    }
    const std::vector<const Variable*>& parameters = this->parameters();
    const std::vector<const Variable*>& otherParameters = f.parameters();
    if (parameters.size() != otherParameters.size()) {
        return false;
    }
    for (size_t i = 0; i < parameters.size(); i++) {
        if (!parameters[i]->type().matches(otherParameters[i]->type())) {
            return false;
        }
    }
    return true;
}

bool FunctionDeclaration::determineFinalTypes(const ExpressionArray& arguments,
                                              ParamTypes* outParameterTypes,
                                              const Type** outReturnType) const {
    const std::vector<const Variable*>& parameters = this->parameters();
    SkASSERT(arguments.size() == parameters.size());

    outParameterTypes->reserve_back(arguments.size());
    int genericIndex = -1;
    for (size_t i = 0; i < arguments.size(); i++) {
        // Non-generic parameters are final as-is.
        const Type& parameterType = parameters[i]->type();
        if (!parameterType.isGeneric()) {
            outParameterTypes->push_back(&parameterType);
            continue;
        }
        // We use the first generic parameter we find to lock in the generic index;
        // e.g. if we find `float3` here, all `$genType`s will be assumed to be `float3`.
        if (genericIndex == -1) {
            genericIndex = find_generic_index(arguments[i]->type(), parameterType,
                                              /*allowNarrowing=*/true);
            if (genericIndex == -1) {
                // The passed-in type wasn't a match for ANY of the generic possibilities.
                // This function isn't a match at all.
                return false;
            }
        }
        outParameterTypes->push_back(parameterType.coercibleTypes()[genericIndex]);
    }
    // Apply the generic index to our return type.
    const Type& returnType = this->returnType();
    if (returnType.isGeneric()) {
        if (genericIndex == -1) {
            // We don't support functions with a generic return type and no other generics.
            return false;
        }
        *outReturnType = returnType.coercibleTypes()[genericIndex];
    } else {
        *outReturnType = &returnType;
    }
    return true;
}

}  // namespace SkSL
