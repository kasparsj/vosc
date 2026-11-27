"""Source fixes shared by the private addon staging builds."""

import re
from pathlib import Path


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


def patch_expr_header(text):
    if '#include "ofUtils.h"' in text:
        return text
    return replace_addon_anchor(text, '#include "ofEvents.h"',
                                '#include "ofEvents.h"\n#include "ofUtils.h"',
                                "ofxExpr event include")


def patch_midi_config(text):
    if re.search(r"^linuxaarch64:[ \t]*$", text, re.MULTILINE):
        return text
    armv7 = re.search(r"^linuxarmv7l:[ \t]*\n(.*?)(?=^[\w/-]+:[ \t]*$|\Z)",
                      text, re.MULTILINE | re.DOTALL)
    if armv7 is None:
        return text
    # The ARM64 SDK enables JACK when compiling RtMidi, which needs a direct link.
    return (
        text + ("" if text.endswith("\n") else "\n")
        + "\nlinuxaarch64:\n" + armv7[1].rstrip()
        + "\n\tADDON_PKG_CONFIG_LIBRARIES += jack\n"
    )


LINUX_LEGACY_PATCHES = {
    "ofxExpr": (("src/ofxExpr.cpp", EXPR_COMPILE_LOOP, patch_expression_returns),
                ("src/ofxExprParser.hpp", '#include "ofEvents.h"', patch_expr_header)),
    "ofxMidi": (("addon_config.mk", "linuxarmv7l:", patch_midi_config),),
    "ofxPostProcessing": (("src/PostProcessing.cpp", POST_PROCESSING_ATTRIBUTE, patch_post_processing),),
    "ofxShadertoy": (("src/ofxShadertoy.cpp", SHADERTOY_TIMESTAMP + ";", patch_shadertoy_timestamp),),
}


def linux_addon_updates(addon):
    updates = {}
    for filename, anchor, patch in LINUX_LEGACY_PATCHES.get(addon.name, ()):
        source = addon / filename
        if not source.is_file():
            continue
        text = source.read_text(encoding="latin-1")
        if anchor in text and (patched := patch(text)) != text:
            updates[Path(filename)] = patched.encode("latin-1")
    return updates
