#version 400

/**
 * SIEVE SHADER
 *
 * This shader implements a "sieve" or spatial remapping effect. It uses a sieve
 * texture to determine how to sample from a data texture, creating a grid-based
 * pixel remapping effect.
 *
 * CONCEPT:
 * - The sieve texture acts as a lookup table containing (x, y) offsets
 * - Each pixel reads the sieve to find which pixel to fetch from the data texture
 * - Allows creating patterns, pixel sorting, or other spatial transformations
 *
 * USE CASES:
 * - Pixel sorting effects
 * - Grid-based transformations
 * - Kaleidoscope/tiling effects
 * - Data visualization with custom sampling patterns
 */

// Input from vertex shader
in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

// Uniforms
uniform ivec2 dim;           // Dimensions of the output rendering area
uniform sampler2D tex;       // Source data texture to sample from
uniform ivec2 texdim;        // Dimensions of the data texture (unused - reserved for future use)
uniform sampler2D sieve;     // Sieve texture containing offset information
uniform ivec2 sievedim;      // Dimensions of the sieve texture
uniform vec4 vColor;         // Vertical color tint
uniform vec4 hColor;         // Horizontal/highlight color
uniform vec2 multiplier;     // Scale multiplier for the sieve grid

uniform float time;          // Time uniform (currently unused)

out vec4 fragColor;

// Constants
const float CHANNEL_MAX = 255.0;  // Maximum value for color channel (8-bit)

// ----------------------------------------------------------------------
// UTILITY FUNCTIONS
// ----------------------------------------------------------------------

/**
 * Branchless equality check
 * Returns 1.0 if x == y, otherwise 0.0
 * Used for conditional blending without branching
 */
float when_eq(float x, float y) {
    return 1.0 - abs(sign(x - y));
}

/**
 * Branchless less-than check
 * Returns 1.0 if x < y, otherwise 0.0
 * Used for conditional blending without branching
 */
float when_lt(float x, float y) {
    return max(sign(y - x), 0.0);
}

// ----------------------------------------------------------------------
// MAIN FRAGMENT SHADER
// ----------------------------------------------------------------------

/**
 * Main shader entry point
 *
 * ALGORITHM:
 * 1. Convert vertex position to pixel coordinates (0 to dim)
 * 2. Read sieve texture to get (x, y) offset encoded in blue/green channels
 * 3. Calculate which pixel to fetch from data texture using sieve offsets
 * 4. Apply conditional color blending based on sieve alpha and red channels:
 *    - If alpha == 1.0: Use sieve color with vColor tint
 *    - If alpha < 1.0: Use data texture color
 *    - If red == 0.0 and alpha == 1.0: Add hColor highlight
 */
void main(){
    // -------------------------------------------------------------------------
    // STEP 1: Convert vertex position to pixel coordinates
    // -------------------------------------------------------------------------

    // Transform from centered coordinates (-dim/2 to +dim/2) to (0 to dim)
    vec2 position = vertex.position.xy + vec2(dim) / 2.0;

    // Optional: Animate sieve position over time (currently disabled)
    // vec2 t = mod(vec2(position.x, position.y + int(time)), vec2(sievedim));
    // vec2 sievePos = t / vec2(sievedim); // 0 to 1

    // -------------------------------------------------------------------------
    // STEP 2: Sample the sieve texture to get offset information
    // -------------------------------------------------------------------------

    // Normalize position to 0-1 range for texture sampling
    vec2 sievePos = position / vec2(sievedim);
    vec4 sievePix = texture(sieve, sievePos);

    // Extract x, y offsets from blue and green channels
    // Channels store normalized values (0-1), convert to pixel coordinates (0-255)
    ivec2 offset = ivec2(
        int(round(sievePix.b * CHANNEL_MAX)),  // Blue channel = x offset
        int(round(sievePix.g * CHANNEL_MAX))   // Green channel = y offset
    );

    // -------------------------------------------------------------------------
    // STEP 3: Calculate data texture sampling position
    // -------------------------------------------------------------------------

    // Calculate the size of each sieve cell in the grid
    vec2 sieves = vec2(sievedim) / multiplier;

    // Complex remapping calculation:
    // 1. floor(position / multiplier / sieves) - Determine which sieve grid cell we're in
    // 2. * sieves - Convert back to pixel coordinates (base position of the cell)
    // 3. + vec2(offset) - Add the offset from sieve texture
    // 4. + 0.5 - Center the sample for better interpolation
    // 5. / (vec2(dim) / multiplier) - Normalize to 0-1 texture coordinates
    // 6. clamp() - Ensure we stay within valid texture bounds
    vec2 dataPos = clamp(
        (floor(position / multiplier / sieves) * sieves + vec2(offset) + 0.5) / (vec2(dim) / multiplier),
        0.0,
        1.0
    );

    // Sample the data texture at the calculated position
    vec4 dataPix = texture(tex, dataPos);

    // -------------------------------------------------------------------------
    // STEP 4: Blend colors based on sieve properties
    // -------------------------------------------------------------------------

    vec3 colorPix = vec3(0.0);

    // If sieve alpha == 1.0: use sieve color with vColor tint
    colorPix += sievePix.rgb * vColor.rgb * when_eq(sievePix.a, 1.0);

    // If sieve alpha < 1.0: use data texture color
    colorPix += dataPix.rgb * when_lt(sievePix.a, 1.0);

    // If sieve red == 0.0 AND alpha == 1.0: add hColor highlight
    colorPix += hColor.rgb * when_eq(sievePix.r, 0.0) * when_eq(sievePix.a, 1.0);

    fragColor = vec4(colorPix, 1.0);
}
