#version 120

const vec3 DEFAULT_COLOR = vec3(1.0);
const int MAX_VALUES = 8;

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float values[MAX_VALUES];

out vec4 fragColor;

varying vec3 vertex;

void main() {
  vec3 coord = vertex.xyz;

  // Compute anti-aliased world-space grid lines
  vec3 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
  float line = min(min(grid.x, grid.y), grid.z);

  // Just visualize the grid lines directly
  vec4 col = color;
  if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
  col = vec4(col.rgb - min(line, 1.0), col.a);

  // Apply gamma correction
  col.rgb = pow(col.rgb, vec3(1.0 / 2.2));
  fragColor = col;
}
