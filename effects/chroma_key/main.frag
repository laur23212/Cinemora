#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;

uniform float u_keyHue;    // backdrop hue in degrees: 120 = green, 240 = blue
uniform float u_tolerance; // how far off the key hue still counts as background
uniform float u_softness;  // width of the alpha ramp at the edge
uniform float u_spill;     // how hard to pull the key colour out of what remains
uniform float u_pedestal;      // 0..1: clears what the key left almost transparent (Filmora's Pedestal)
uniform float u_edgeThickness; // px: below 0 pulls the subject's edge in, above 0 pushes it out
uniform float u_alphaView;     // 1: show the matte (white = kept) instead of the picture
uniform vec2 u_resolution;

const vec3 kRec709 = vec3(0.2126, 0.7152, 0.0722);

// Chroma magnitude scales with brightness, so a shadowed fold of the screen reads
// as far less green than a lit one and survives the key. Dividing the score by
// luma removes that; the floor keeps near-black pixels — where chroma is mostly
// sensor noise — from being amplified into a false key.
const float kLumaFloor = 0.15;

vec2 chromaOf(vec3 c, float y)
{
    return vec2((c.b - y) * 0.5389, (c.r - y) * 0.6350);
}

vec3 hueToRgb(float degrees)
{
    float h = mod(degrees, 360.0) / 60.0;
    float x = 1.0 - abs(mod(h, 2.0) - 1.0);
    if (h < 1.0) return vec3(1.0, x, 0.0);
    if (h < 2.0) return vec3(x, 1.0, 0.0);
    if (h < 3.0) return vec3(0.0, 1.0, x);
    if (h < 4.0) return vec3(0.0, x, 1.0);
    if (h < 5.0) return vec3(x, 0.0, 1.0);
    return vec3(1.0, 0.0, x);
}

// How much of a pixel the key keeps, 0 (backdrop) to 1 (subject).
float keyAlpha(vec3 rgb, vec2 keyDir)
{
    float y = dot(rgb, kRec709);
    vec2 pc = chromaOf(rgb, y);
    float along = dot(pc, keyDir);
    float across = length(pc - along * keyDir);

    float norm = max(y, kLumaFloor);
    float score = (along - across) / norm;

    // Tolerance sets the centre of the alpha ramp rather than its start. A pixel on
    // an antialiased or motion-blurred edge is part screen and part subject, so its
    // score lands between the two; a ramp starting at the screen's score would leave
    // that whole blend opaque and ring the subject with a hard fringe. Centred, the
    // blend falls through the ramp and comes out partly transparent, as it should.
    float centre = mix(0.75, 0.05, clamp(u_tolerance, 0.0, 1.0));
    float width = mix(0.05, 0.60, clamp(u_softness, 0.0, 1.0));
    return 1.0 - smoothstep(centre - width * 0.5, centre + width * 0.5, score);
}

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);

    // Effect parameters are plain floats, so the key is a hue rather than a picked
    // colour: we never learn how saturated the actual backdrop is. Splitting the
    // pixel's chroma into a component along the key hue and one across it keys on
    // hue alone, which holds up whether the screen is vivid or washed out.
    vec3 keyRgb = hueToRgb(u_keyHue);
    vec2 keyDir = normalize(chromaOf(keyRgb, dot(keyRgb, kRec709)));

    float y = dot(src.rgb, kRec709);
    vec2 pc = chromaOf(src.rgb, y);
    float along = dot(pc, keyDir);
    float alpha = keyAlpha(src.rgb, keyDir);

    // Edge Thickness: the matte's smallest (thinner) or largest (thicker) value over a ring of
    // that many pixels — an erode or a dilate of the subject's edge. 0, the default, skips it.
    float radius = min(abs(u_edgeThickness), 10.0);
    if (radius > 0.01) {
        vec2 ring = vec2(radius) / max(u_resolution, vec2(1.0));
        float thinner = alpha;
        float thicker = alpha;
        for (int i = 0; i < 8; ++i) {
            float angle = float(i) * 0.78539816;
            vec2 uv = v_texCoord + vec2(cos(angle), sin(angle)) * ring;
            float a = keyAlpha(texture(u_currentTexture, uv).rgb, keyDir);
            thinner = min(thinner, a);
            thicker = max(thicker, a);
        }
        alpha = u_edgeThickness < 0.0 ? thinner : thicker;
    }

    // Pedestal lifts the floor of the matte: what the key left nearly see-through (noise in
    // the screen, a faint shadow) goes fully transparent. 0, the default, changes nothing.
    float pedestal = clamp(u_pedestal, 0.0, 1.0) * 0.5;
    alpha = clamp((alpha - pedestal) / (1.0 - pedestal), 0.0, 1.0);

    // Alpha Channel: the matte itself, white where the subject is kept, to judge the key by.
    if (u_alphaView > 0.5) {
        fragColor = vec4(vec3(alpha * src.a), 1.0);
        return;
    }

    // Screen bounce tints what survives, worst along the subject's edge. Subtract the
    // key-hue component of the chroma and rebuild RGB: luma and the across-axis colour
    // survive, so a warm or blue foreground keeps its character instead of going grey.
    // Removal scales up as alpha falls — a mostly-screen pixel is mostly screen colour,
    // and alpha here is straight, not premultiplied, so that colour still tints the
    // composite and would otherwise ring the subject with a halo.
    float spillStrength = mix(1.0, clamp(u_spill, 0.0, 1.0), alpha);
    vec2 corrected = pc - max(along, 0.0) * spillStrength * keyDir;
    float r = corrected.y / 0.6350 + y;
    float b = corrected.x / 0.5389 + y;
    float g = (y - 0.2126 * r - 0.0722 * b) / 0.7152;

    fragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), src.a * alpha);
}
