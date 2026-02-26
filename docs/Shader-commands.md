## Overview

- `/shader` [Load layer shader](#shader-load)
- `/shader/var` [Set layer shader variable](#shader-var)
- `/shader/texture` [Set layer shader texture](#shader-texture)
- `/shader/set` [Set layer shader property](#shader-set)

## Reference

### Shader load

`/shader`

Arguments:
- **target** (int|string) layer index or wildcard, e.g. "*"
- **source** (string) source code or shader file path or shadertoy.com URL

Examples:
```supercollider
~visuals.sendMsg('/shader', 0, "
#version 150
uniform float time;
uniform vec2 resolution;
out vec4 fragColor;
void main() {
  vec2 uv = gl_FragCoord.xy/resolution.xy;
  vec3 col = 0.5 + 0.5*cos(time+uv.xyx+vec3(0,2,4));
  fragColor = vec4(col,1.0);
}
"); // load a basic shader from source into first layer

~visuals.sendMsg('/shader', 0, "shaders/shadertoy/Water.frag"); // load fragment shader from file into first layer

~visuals.sendMsg('/shader', 0, "particles/DrawInstancedGeometry"); // load fragment and vertex shaders

~visuals.sendMsg('/shader', 0, "grass/Grass"); // load fragment and geometry shaders

~visuals.sendMsg('/shader', 0, "https://www.shadertoy.com/view/MtSBDc"); // load a shadertoy into first layer
```

### Shader var

`/shader/var`

Arguments:
- **target** (int|string) layer index or wildcard, e.g. "*"
- **name** (string) variable name
- **value1** (mixed) value
- value2 (mixed) value
- value3 (mixed) value

### Shader texture

`/shader/texture`

Arguments:
- **target** (int|string) layer index or wildcard, e.g. "*"
- **name** (string) uniform name
- **value** (mixed) texture load path or shared texture name

### Shader set

`/shader/set`

Arguments:
- **target** (int|string) layer index or wildcard, e.g. "*"
- **name** (string) property name
- **value** (mixed) property value

Examples:
```supercollider
~visuals.sendMsg('/shader/set', 0, "geomInputType", 0x0001); // set first layer shader geomInputType to GL_LINES
~visuals.sendMsg('/shader/set', 0, "geomOutputType", 0x0005); // set first layer shader geomOutputType to GL_TRIANGLE_STRIP
~visuals.sendMsg('/shader/set', 0, "geomOutputCount", 14); // set first layer shader geomOutputCount to 14
```