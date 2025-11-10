#version 400

/**
 * PALETTE MAPPER SHADER
 *
 * This shader reduces colors to a limited palette by finding the closest
 * matching color from a palette texture. It supports different color
 * distance algorithms (RGB and CIELAB Delta E 2000).
 *
 * LUMINANCE PRESERVATION:
 * The shader includes an optional luminance weighting feature that prevents
 * inverted colors (e.g., black vs white) from mapping to the same palette color.
 * When enabled, dark input colors prefer dark palette colors, and light input
 * colors prefer light palette colors.
 *
 * The algorithm uses:
 * - Linear penalty for small luminance differences (<0.3)
 * - Exponential penalty for large luminance differences (>0.3)
 * This heavily discourages very dark colors from mapping to very light palette
 * colors and vice versa.
 *
 * Controlled via luminanceWeight uniform:
 * - 0.0 = off (standard color matching)
 * - 1.0-2.0 = moderate (recommended for most cases)
 * - 3.0+ = strong (aggressive luminance preservation)
 *
 * Use cases:
 * - Retro graphics effects (limited color palettes)
 * - Color quantization
 * - Artistic/stylized rendering
 */

#ifndef PI
    #define PI       3.14159265358979323846
#endif
#ifndef DEG_TO_RAD
    #define DEG_TO_RAD (PI/180.0)
#endif
#ifndef EPSILON
    #define EPSILON  0.0001  // Epsilon for float comparisons
#endif

// Input from vertex shader
in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

// Uniforms
uniform vec2 size;              // The size of the rendering area
uniform samplerBuffer palette; // Buffer texture containing the color palette
uniform int algorithm;         // 0=NONE (passthrough, no palette mapping)
                               // 1=RGB_DIFF (RGB Euclidean distance)
                               // 2=LAB_DIFF (CIEDE2000 perceptual distance)
uniform int error;             // Error dithering parameter
uniform float offset;          // Color offset (0.0-1.0) for animation/variation
uniform float luminanceWeight; // Weight for luminance preservation (0.0=off, higher=stronger)
                               // Prevents inverted colors from mapping to same palette color

out vec4 fragColor;

// ----------------------------------------------------------------------
// UTILITY FUNCTIONS
// ----------------------------------------------------------------------

/**
 * Convert degrees to radians
 */
float degToRad(float degrees) {
    return degrees * DEG_TO_RAD;
}

/**
 * Calculate perceived luminance of an RGB color
 *
 * Uses the standard Rec. 709 luma coefficients which match human
 * perception of brightness (we're more sensitive to green than red/blue).
 *
 * @param color RGB color (normalized 0-1)
 * @return Luminance value (0-1, 0=black, 1=white)
 */
float getLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

/**
 * Calculate RGB Euclidean distance between two colors
 *
 * Simple distance metric - treats RGB as a 3D euclidean space.
 * Fast but not perceptually accurate (colors that look similar to
 * humans may have large RGB distances and vice versa).
 *
 * @param c1 First color (normalized 0-1)
 * @param c2 Second color (normalized 0-1)
 * @param lumWeight Weight for luminance preservation (0.0=off, higher=stronger)
 * @return Distance value (larger = more different)
 */
float diffRGB(vec3 c1, vec3 c2, float lumWeight) {
    // Standard RGB Euclidean distance
    int r = int((c1.r - c2.r) * 255.0);
    int g = int((c1.g - c2.g) * 255.0);
    int b = int((c1.b - c2.b) * 255.0);
    float rgbDist = sqrt(r*r + g*g + b*b);

    // Add luminance penalty to discourage inverted colors mapping to same palette color
    // This makes dark colors prefer dark palette colors and light colors prefer light ones
    if (lumWeight > 0.0) {
        float lum1 = getLuminance(c1);
        float lum2 = getLuminance(c2);
        float lumDiff = abs(lum1 - lum2);

        // Use exponential penalty for large luminance differences
        // This heavily penalizes mapping very dark colors to very light ones (and vice versa)
        // For small differences: mostly linear penalty
        // For large differences (>0.3): exponential growth
        float lumPenalty;
        if (lumDiff > 0.3) {
            // Exponential penalty for inverted colors
            lumPenalty = 255.0 * lumDiff * lumWeight * (1.0 + pow(lumDiff * 2.0, 2.0));
        } else {
            // Linear penalty for similar luminance
            lumPenalty = 255.0 * lumDiff * lumWeight;
        }

        // Weighted combination: RGB distance + luminance penalty
        return rgbDist + lumPenalty;
    }

    return rgbDist;
}

/**
 * Calculate CIEDE2000 (Delta E 2000) color difference in LAB color space
 *
 * This is a perceptually-uniform color difference metric developed by the
 * International Commission on Illumination (CIE). It accounts for how
 * humans actually perceive color differences, making it much more accurate
 * than simple RGB distance.
 *
 * The formula includes corrections for:
 * - Lightness (L*)
 * - Chroma (C*)
 * - Hue (H*)
 * - Interaction between chroma and hue
 *
 * @param lab1 First color in LAB space (L: 0-100, a: -128-127, b: -128-127)
 * @param lab2 Second color in LAB space
 * @param lumWeight Weight for luminance preservation (0.0=standard, higher=stronger L* weight)
 * @return Delta E 2000 value (smaller = more similar colors)
 *         Values < 1.0 are imperceptible to humans
 *         Values 1.0-2.0 are perceptible with close observation
 *         Values > 2.0 are clearly different colors
 */
float diffLab(vec3 lab1, vec3 lab2, float lumWeight) {
    /*
     * "For these and all other numerical/graphical 􏰀delta E00 values
     * reported in this article, we set the parametric weighting factors
     * to unity(i.e., k_L = k_C = k_H = 1.0)." (Page 27).
     *
     * However, we can adjust k_L to increase lightness sensitivity and
     * prevent inverted colors from mapping to the same palette color.
     */
    // Weighting factors for lightness, chroma, and hue
    // Decrease k_L based on lumWeight to prioritize lightness matching
    // (smaller k_L means lightness differences contribute more to the final deltaE)
    // Using more aggressive scaling for stronger luminance preservation
    float k_L = 1.0 / max(1.0, (1.0 + lumWeight * 1.5));
    const float k_C = 1.0;
    const float k_H = 1.0;

    float deg360InRad = degToRad(360.0);
    float deg180InRad = degToRad(180.0);
    const float pow25To7 = 6103515625.0; /* pow(25, 7) - precomputed constant */

    // -------------------------------------------------------------------------
    // STEP 1: Calculate C (chroma), adjust a* values, calculate C' and h'
    // -------------------------------------------------------------------------

    // Calculate chroma (saturation) from a* and b* components
    float C1 = sqrt((lab1.y * lab1.y) + (lab1.z * lab1.z));  // Eq. 2
    float C2 = sqrt((lab2.y * lab2.y) + (lab2.z * lab2.z));

    // Average chroma
    float barC = (C1 + C2) / 2.0;  // Eq. 3

    // G factor adjusts for non-linearity in chroma perception
    float G = 0.5 * (1 - sqrt(pow(barC, 7) / (pow(barC, 7) + pow25To7)));  // Eq. 4

    // Adjust a* values using G factor
    float a1Prime = (1.0 + G) * lab1.y;  // Eq. 5
    float a2Prime = (1.0 + G) * lab2.y;

    // Recalculate chroma with adjusted a* values
    float CPrime1 = sqrt((a1Prime * a1Prime) + (lab1.z * lab1.z));  // Eq. 6
    float CPrime2 = sqrt((a2Prime * a2Prime) + (lab2.z * lab2.z));

    // Calculate hue angle (in radians) - Eq. 7
    // Formula: h = atan2(b*, a*) where lab.z = b* and aPrime = a*
    float hPrime1;
    if (abs(lab1.z) < EPSILON && abs(a1Prime) < EPSILON) {
        hPrime1 = 0.0;  // Achromatic (gray) - no hue
    } else {
        hPrime1 = atan(lab1.z, a1Prime);  // atan(y, x) = atan2(b*, a*)
        // Normalize to 0-360 degrees (in radians)
        if (hPrime1 < 0)
            hPrime1 += deg360InRad;
    }

    float hPrime2;
    if (abs(lab2.z) < EPSILON && abs(a2Prime) < EPSILON) {
        hPrime2 = 0.0;  // Achromatic (gray) - no hue
    } else {
        hPrime2 = atan(lab2.z, a2Prime);  // atan(y, x) = atan2(b*, a*)
        // Normalize to 0-360 degrees (in radians)
        if (hPrime2 < 0)
            hPrime2 += deg360InRad;
    }

    // -------------------------------------------------------------------------
    // STEP 2: Calculate differences in L', C', and h'
    // -------------------------------------------------------------------------

    // Lightness difference
    float deltaLPrime = lab2.x - lab1.x;  // Eq. 8

    // Chroma difference
    float deltaCPrime = CPrime2 - CPrime1;  // Eq. 9

    // Hue difference (accounts for circular nature of hue)
    float deltahPrime;  // Eq. 10
    float CPrimeProduct = CPrime1 * CPrime2;
    if (CPrimeProduct < EPSILON) {
        deltahPrime = 0;  // Both colors are achromatic
    } else {
        deltahPrime = hPrime2 - hPrime1;
        // Normalize to -180 to +180 degrees
        if (deltahPrime < -deg180InRad)
            deltahPrime += deg360InRad;
        else if (deltahPrime > deg180InRad)
            deltahPrime -= deg360InRad;
    }

    // Convert hue difference to chroma-scaled difference
    float deltaHPrime = 2.0 * sqrt(CPrimeProduct) * sin(deltahPrime / 2.0);  // Eq. 11

    // -------------------------------------------------------------------------
    // STEP 3: Calculate mean values and weighting functions
    // -------------------------------------------------------------------------

    // Average lightness
    float barLPrime = (lab1.x + lab2.x) / 2.0;  // Eq. 12

    // Average chroma
    float barCPrime = (CPrime1 + CPrime2) / 2.0;  // Eq. 13

    // Average hue (accounts for circular nature) - Eq. 14
    float barhPrime, hPrimeSum = hPrime1 + hPrime2;
    if (CPrime1 * CPrime2 < EPSILON) {
        barhPrime = hPrimeSum;  // One or both colors achromatic
    } else {
        if (abs(hPrime1 - hPrime2) <= deg180InRad) {
            barhPrime = hPrimeSum / 2.0;
        } else {
            // Handle hues on opposite sides of 0/360 degrees
            if (hPrimeSum < deg360InRad)
                barhPrime = (hPrimeSum + deg360InRad) / 2.0;
            else
                barhPrime = (hPrimeSum - deg360InRad) / 2.0;
        }
    }

    // Hue rotation term - compensates for hue-dependent perception - Eq. 15
    float T = 1.0 - (0.17 * cos(barhPrime - degToRad(30.0))) +
                    (0.24 * cos(2.0 * barhPrime)) +
                    (0.32 * cos((3.0 * barhPrime) + degToRad(6.0))) -
                    (0.20 * cos((4.0 * barhPrime) - degToRad(63.0)));

    // Rotation term for blue colors - Eq. 16
    float deltaTheta = degToRad(30.0) * exp(-pow((barhPrime - degToRad(275.0)) / degToRad(25.0), 2.0));

    // Chroma rotation factor - Eq. 17
    float R_C = 2.0 * sqrt(pow(barCPrime, 7.0) / (pow(barCPrime, 7.0) + pow25To7));

    // Lightness weighting function - compensates for lightness-dependent perception - Eq. 18
    float S_L = 1 + ((0.015 * pow(barLPrime - 50.0, 2.0)) / sqrt(20 + pow(barLPrime - 50.0, 2.0)));

    // Chroma weighting function - Eq. 19
    float S_C = 1 + (0.045 * barCPrime);

    // Hue weighting function - Eq. 20
    float S_H = 1 + (0.015 * barCPrime * T);

    // Chroma-hue interaction term - Eq. 21
    float R_T = (-sin(2.0 * deltaTheta)) * R_C;

    // Final CIEDE2000 color difference - Eq. 22
    float deltaE = sqrt(
        pow(deltaLPrime / (k_L * S_L), 2.0) +
        pow(deltaCPrime / (k_C * S_C), 2.0) +
        pow(deltaHPrime / (k_H * S_H), 2.0) +
        (R_T * (deltaCPrime / (k_C * S_C)) * (deltaHPrime / (k_H * S_H)))
    );

    // Add additional exponential penalty for very different lightness values
    // This further discourages inverted colors from mapping to the same palette color
    if (lumWeight > 0.0) {
        float L_diff_normalized = abs(deltaLPrime) / 100.0;  // Normalize L* difference to 0-1
        if (L_diff_normalized > 0.3) {
            // Apply exponential penalty for large lightness differences
            float extraPenalty = deltaE * lumWeight * pow(L_diff_normalized * 2.0, 2.0);
            deltaE += extraPenalty;
        }
    }

    return deltaE;
}

// ----------------------------------------------------------------------
// COLOR SPACE CONVERSION FUNCTIONS
// ----------------------------------------------------------------------

/**
 * Convert packed integer (0xRRGGBB) to normalized RGB vector
 *
 * @param val Integer color value in format 0xRRGGBB
 * @return vec3 with RGB components in range 0.0-1.0
 */
vec3 toRGB(int val) {
    float rValue = (((val) >> 16) & 0xFF) / 255.0;  // Extract red
    float gValue = (((val) >> 8) & 0xFF) / 255.0;   // Extract green
    float bValue = ((val) & 0xFF) / 255.0;          // Extract blue
    return vec3(rValue, gValue, bValue);
}

/**
 * Convert sRGB color to CIE XYZ color space
 *
 * XYZ is a device-independent color space that models human color perception.
 * It's an intermediate step in converting RGB to LAB.
 *
 * @param color sRGB color (normalized 0-1)
 * @return XYZ color (X: 0-95.047, Y: 0-100, Z: 0-108.883)
 */
vec3 toXYZ(vec3 color) {
    float r = color.r;
    float g = color.g;
    float b = color.b;

    // Apply inverse sRGB gamma correction (linearize)
    // sRGB uses a piecewise function for gamma
    if (r > 0.04045) {
        r = pow((r + 0.055) / 1.055, 2.4);
    } else {
        r = r / 12.92;
    }
    if (g > 0.04045) {
        g = pow((g + 0.055) / 1.055, 2.4);
    } else {
        g = g / 12.92;
    }
    if (b > 0.04045) {
        b = pow((b + 0.055) / 1.055, 2.4);
    } else {
        b = b / 12.92;
    }

    // Scale to 0-100 range
    r *= 100;
    g *= 100;
    b *= 100;

    // Transform to XYZ using standard matrix for D65 illuminant, 2° observer
    float x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    float y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    float z = r * 0.0193 + g * 0.1192 + b * 0.9505;
    return vec3(x, y, z);
}

/**
 * Convert CIE XYZ color to CIE LAB color space
 *
 * LAB is designed to be perceptually uniform - equal distances in LAB space
 * correspond to equal perceived color differences.
 *
 * L* = Lightness (0-100, 0=black, 100=white)
 * a* = Green-Red axis (negative=green, positive=red)
 * b* = Blue-Yellow axis (negative=blue, positive=yellow)
 *
 * @param xyz XYZ color
 * @return LAB color (L: 0-100, a: ~-128-127, b: ~-128-127)
 */
vec3 XYZToLab(vec3 xyz) {
    // Normalize by D65 white point reference values
    float x = xyz.x / 95.047;   // Observer = 2°, Illuminant = D65
    float y = xyz.y / 100.000;
    float z = xyz.z / 108.883;

    // Apply piecewise function for perceptual uniformity
    // Values > 0.008856 use cube root, smaller values use linear approximation
    if (x > 0.008856) {
        x = pow(x, 1.0/3.0);
    } else {
        x = (7.787 * x) + 16.0/116.0;
    }
    if (y > 0.008856) {
        y = pow(y, 1.0/3.0);
    } else {
        y = (7.787 * y) + 16.0/116.0;
    }
    if (z > 0.008856) {
        z = pow(z, 1.0/3.0);
    } else {
        z = (7.787 * z) + 16.0/116.0;
    }

    // Calculate L*, a*, b* values
    float L = (116 * y) - 16;      // Lightness
    float a = 500 * (x - y);       // Green-Red
    float b = 200 * (y - z);       // Blue-Yellow
    return vec3(L, a, b);
}

/**
 * Convert sRGB directly to CIE LAB color space
 * Convenience function that chains toXYZ() and XYZToLab()
 */
vec3 toLab(vec3 color) {
    return XYZToLab(toXYZ(color));
}

/**
 * Generate a deterministic color based on position
 *
 * Creates a unique color for each pixel position that can be animated
 * using the offset parameter. The error parameter adds dithering variation.
 *
 * @param pos Pixel position (from vertex.position.xy)
 * @param offset Animation offset (0.0-1.0)
 * @param size Size of the rendering area (vec2 with width and height)
 * @param error Error dithering parameter
 * @return RGB color (normalized 0-1)
 */
vec3 generateColorFromPosition(vec2 pos, float offset, vec2 size, int error) {
    // Calculate scaling factors (incorporates error dithering)
    // Use separate scaling for x and y to handle non-square rendering areas
    float kx = size.x / float(4096 + error);
    float ky = size.y / float(4096 + error);

    // Generate a unique integer color value based on:
    // - Pixel position (x, y)
    // - Animated offset (for color cycling effects)
    // - Error dithering parameter
    // This creates a color lookup table (LUT) that covers the full color space
    // including black (0,0,0) so source images can map all colors to the palette
    int colorValue = int(mod(
        offset * 0xFFFFFF +
        int((pos.x + size.x / 2.0) * (4096 + error) / kx +
            (pos.y + size.y / 2.0) / ky),
//            (pos.y + size.y / 2.0) * (4096 + error) / ky),
        0xFFFFFF
    ));

    return toRGB(colorValue);
}

// ----------------------------------------------------------------------
// MAIN FRAGMENT SHADER
// ----------------------------------------------------------------------

/**
 * Main shader entry point
 *
 * This shader maps each pixel to the closest color in a limited palette.
 *
 * ALGORITHM:
 * 1. Generate a unique color for this pixel based on position
 * 2. If algorithm==0 (NONE): output that color directly (no palette mapping)
 * 3. Otherwise: find the closest matching color in the palette
 *    - algorithm==1 (RGB_DIFF): Use RGB Euclidean distance
 *    - algorithm==2 (LAB_DIFF): Use perceptually-accurate CIEDE2000 distance
 *    Both algorithms support optional luminance weighting to prevent
 *    inverted colors from mapping to the same palette color.
 * 4. Output the closest palette color (ONLY colors from palette are output)
 *
 * The position-based color generation creates a deterministic but varied
 * color field that can be animated using the 'offset' parameter.
 *
 * LUMINANCE WEIGHTING:
 * Set luminanceWeight to control how much the shader prioritizes matching
 * brightness levels:
 * - 0.0: Off - standard color matching only
 * - 1.0-2.0: Moderate - balanced brightness and color matching (recommended)
 * - 3.0+: Strong - heavily prioritizes brightness matching
 *
 * The algorithm applies exponential penalties for large luminance differences
 * (>0.3), making it very expensive for inverted colors to map to the same
 * palette color. For example, black and white will strongly prefer different
 * palette colors when luminanceWeight > 0.
 */
void main() {
    // -------------------------------------------------------------------------
    // STEP 1: Generate source color based on pixel position
    // -------------------------------------------------------------------------

    vec3 colorRGB = generateColorFromPosition(vertex.position.xy, offset, size, error);

    // -------------------------------------------------------------------------
    // STEP 2: Check for passthrough mode (no palette mapping)
    // -------------------------------------------------------------------------

    if (algorithm == 0) {
        fragColor = vec4(colorRGB, 1.0);
        return;
    }

    // -------------------------------------------------------------------------
    // STEP 3: Find closest color in palette
    // -------------------------------------------------------------------------

    // Initialize with first palette color
    vec4 closest = texelFetch(palette, 0);
    float diff;

    // Get palette size from texture
    int paletteSize = textureSize(palette);

    // Use proper branching for better performance (avoids computing both distances)
    if (algorithm == 1) {
        // RGB distance algorithm with optional luminance preservation
        diff = diffRGB(closest.rgb, colorRGB, luminanceWeight);

        // Search through all colors in the palette
        for (int i = 0; i < paletteSize; i++) {
            if (i == 0) continue;

            vec4 candidateColor = texelFetch(palette, i);
            float candidateDiff = diffRGB(candidateColor.rgb, colorRGB, luminanceWeight);

            if (candidateDiff < diff) {
                diff = candidateDiff;
                closest = candidateColor;
            }
        }
    } else {
        // LAB distance algorithm (CIEDE2000) with optional luminance preservation
        vec3 colorLab = toLab(colorRGB);
        vec3 closestLab = toLab(closest.rgb);
        diff = diffLab(closestLab, colorLab, luminanceWeight);

        // Search through all colors in the palette
        for (int i = 0; i < paletteSize; i++) {
            if (i == 0) continue;

            vec4 candidateColor = texelFetch(palette, i);
            vec3 candidateLab = toLab(candidateColor.rgb);
            float candidateDiff = diffLab(candidateLab, colorLab, luminanceWeight);

            if (candidateDiff < diff) {
                diff = candidateDiff;
                closest = candidateColor;
            }
        }
    }

    // -------------------------------------------------------------------------
    // STEP 4: Output the closest palette color
    // -------------------------------------------------------------------------

    fragColor = closest;
}

