#version 330 core

// 灵境 — 水波涟漪着色器
// 用于 RippleWidget 的硬件加速渲染

in vec2 vTexCoord;
in vec2 vPosition;

out vec4 fragColor;

uniform sampler2D uBackground;
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uRippleCenters[8];
uniform float uRippleTimes[8];
uniform int uRippleCount;
uniform float uStrength;

// 高斯衰减
float gaussian(float d, float r) {
    return exp(-d * d / (r * r));
}

void main() {
    vec2 uv = vTexCoord;
    vec3 color = texture(uBackground, uv).rgb;

    // 水波扰动
    vec2 distortion = vec2(0.0);

    for (int i = 0; i < uRippleCount; ++i) {
        vec2 center = uRippleCenters[i].xy / uResolution;
        float rippleTime = uTime - uRippleTimes[i];

        if (rippleTime < 0.0 || rippleTime > 1.5) continue;

        vec2 delta = uv - center;
        float dist = length(delta);
        float radius = rippleTime * 0.5;

        float wave = sin(dist * 30.0 - rippleTime * 20.0);
        float envelope = gaussian(dist - radius, 0.1)
                       * exp(-rippleTime * 2.0);

        distortion += normalize(delta + 1e-6) * wave * envelope * uStrength * 0.02;
    }

    vec2 distortedUV = uv + distortion;

    // 采样扰动后的背景
    vec3 distortedColor = texture(uBackground, distortedUV).rgb;

    // 混合
    fragColor = vec4(mix(color, distortedColor, 0.7), 1.0);
}