#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float audioAmp;
    vec2 resolution;
    float isSpeaking;
    float cornerRadius;
};

// 2D distance to rounded rectangle boundary
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    vec2 p = (qt_TexCoord0 - vec2(0.5)) * resolution;
    vec2 halfRes = resolution * 0.5;
    
    // Smooth corner radius for window edges
    float r = cornerRadius;
    float d = sdRoundedBox(p, halfRes, r);
    float dist = -d;
    
    // Outside the window's rounded contour -> completely transparent
    if (dist < 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    
    // Apple Intelligence Glow Geometry
    // Beam width at perimeter + luminous diffusion inward
    float baseWidth = 9.0;
    float glowWidth = baseWidth + 14.0 * clamp(audioAmp, 0.0, 1.0);
    
    // Deeper inside than glowWidth -> transparent (so the window interior remains clean)
    if (dist > glowWidth) {
        fragColor = vec4(0.0);
        return;
    }
    
    float norm = dist / glowWidth;
    
    // Core beam right at the perimeter edge (0 to 3.5px)
    float edgeCore = smoothstep(3.5, 0.0, dist) * 0.95;
    
    // Soft atmospheric aura diffusing inward
    float aura = pow(1.0 - norm, 2.0) * 0.85;
    
    float alpha = clamp(edgeCore + aura, 0.0, 1.0) * qt_Opacity;
    
    // Spectrum calculation (Apple Intelligence Signature Colors)
    vec2 uv = qt_TexCoord0;
    vec2 center = vec2(0.5, 0.5);
    float angle = atan(uv.y - center.y, uv.x - center.x) + time * 0.85;
    
    // Vibrant Neon Palette (macOS Golden Gate / Apple Intelligence)
    vec3 cCyan    = vec3(0.00, 0.96, 1.00); // #00F5FF Electric Cyan
    vec3 cViolet  = vec3(0.46, 0.15, 0.98); // #7626FA Deep Violet
    vec3 cMagenta = vec3(1.00, 0.08, 0.55); // #FF148C Hot Magenta
    vec3 cAmber   = vec3(1.00, 0.65, 0.00); // #FFA600 Warm Amber
    
    vec3 cTurquoise = vec3(0.05, 0.98, 0.82); // Speaking mode
    vec3 cDeepBlue  = vec3(0.00, 0.55, 1.00);
    
    float s1 = sin(angle);
    float c1 = cos(angle);
    
    vec3 siriColor = mix(
        mix(cCyan, cViolet, clamp(s1 * 0.5 + 0.5, 0.0, 1.0)),
        mix(cMagenta, cAmber, clamp(c1 * 0.5 + 0.5, 0.0, 1.0)),
        0.5 + 0.5 * sin(angle * 2.0)
    );
    
    vec3 speakColor = mix(cTurquoise, cDeepBlue, clamp(s1 * 0.5 + 0.5, 0.0, 1.0));
    vec3 finalColor = mix(siriColor, speakColor, clamp(isSpeaking, 0.0, 1.0));
    
    fragColor = vec4(finalColor * alpha, alpha);
}
