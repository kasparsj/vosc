"""Source fixes shared by the private addon staging builds."""


def replace_addon_anchor(text, original, replacement, description):
    if text.count(original) != 1:
        raise ValueError(f"Expected one {description} anchor in the addon source")
    return text.replace(original, replacement)


EXPR_COMPILE_LOOP = (
    "    for (int i=0; i<size(); i++) {\n        expr[i]->compile(setTime);\n    }\n}"
)
SHADERTOY_TIMESTAMP = "std::filesystem::last_write_time(ofToDataPath(mainShaderFile))"


def patch_expression_returns(text):
    # Preserve evaluation of every component and report any component failure.
    for call in ("addVar(name, value, recompile, setTime)", "addDummyVar(name, recompile)",
                 "addConst(name, value, recompile)", "compile(setTime)"):
        text = replace_addon_anchor(
            text, f"    for (int i=0; i<size(); i++) {{\n        expr[i]->{call};\n    }}\n}}",
            f"    bool success = true;\n    for (int i=0; i<size(); i++) {{\n"
            f"        success = expr[i]->{call} && success;\n    }}\n    return success;\n}}",
            f"vector expression {call}",
        )
    return text


def patch_shadertoy_timestamp(text):
    return replace_addon_anchor(text, SHADERTOY_TIMESTAMP,
                                SHADERTOY_TIMESTAMP + ".time_since_epoch().count()",
                                "Shadertoy last-write time")
