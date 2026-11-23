"""Source fixes shared by the private addon staging builds."""

import re


def replace_addon_anchor(text, original, replacement, description):
    if text.count(original) != 1:
        raise ValueError(f"Expected one {description} anchor in the addon source")
    return text.replace(original, replacement)


EXPR_COMPILE_LOOP = (
    "    for (int i=0; i<size(); i++) {\n        expr[i]->compile(setTime);\n    }\n}"
)
SHADERTOY_TIMESTAMP = "std::filesystem::last_write_time(ofToDataPath(mainShaderFile))"
POST_PROCESSING_ATTRIBUTE = "glPushAttrib(GL_ENABLE_BIT);"


def patch_core_profile_render_state(text, pushes, pops):
    # VOSC uses programmable GL. The fixed-function attribute stack and
    # lighting switch are invalid in core GL; OF style and FBO scope remain.
    for call, expected in ((POST_PROCESSING_ATTRIBUTE, pushes), ("glPopAttrib();", pops),
                           ("glDisable(GL_LIGHTING);", 1)):
        pattern = r"^[ \t]*" + re.escape(call) + r"[ \t]*\r?\n"
        text, actual = re.subn(pattern, "", text, flags=re.MULTILINE)
        if actual != expected:
            raise ValueError(f"Expected {expected} {call} calls in the addon source, found {actual}")
    return text


def patch_post_processing(text):
    return patch_core_profile_render_state(text, 3, 2)


RENDER_ADDON_PATCHES = {
    "ofxPostProcessing/src/PostProcessing.cpp": patch_post_processing,
    "ofxDeferredShading/src/DeferredProcessor.cpp": lambda text: patch_core_profile_render_state(text, 1, 1),
    "ofxDeferredShading/src/GBuffer.cpp": lambda text: replace_addon_anchor(
        text, 'shader.setUniform1f("isShadow", 0);', 'shader.setUniform1i("isShadow", 0);',
        "deferred integer isShadow uniform",
    ),
    "ofxDeferredShading/src/pass/SsaoPass.cpp": lambda text: replace_addon_anchor(
        text, 'calcAo.setUniformTexture("colorTex", gbuffer.getTexture(GBuffer::TYPE_ALBEDO), 2);',
        'applyAo.setUniformTexture("colorTex", gbuffer.getTexture(GBuffer::TYPE_ALBEDO), 2);',
        "deferred SSAO color input on the bound apply program",
    ),
}


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
