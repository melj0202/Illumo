#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vTexCoord;

uniform mat4 uMVP;
uniform mat4 uPrevMVP;
uniform mat4 uModel;
uniform int uMotionBlurEnabled;
uniform float uMotionBlurAmount;
uniform float uMotionBlurMax;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vNormal = normalize(normalMatrix * aNormal);

    vColor = aColor;
    vTexCoord = aTexCoord;

    vec4 currentClip = uMVP * vec4(aPos, 1.0);
    if (uMotionBlurEnabled == 0 || uMotionBlurAmount <= 0.0) {
        gl_Position = currentClip;
        return;
    }

    vec4 previousClip = mix(currentClip, uPrevMVP * vec4(aPos, 1.0),
                            clamp(uMotionBlurAmount, 0.0, 1.0));
    float currentW = max(abs(currentClip.w), 1e-5);
    float previousW = max(abs(previousClip.w), 1e-5);
    vec2 currentNdc = currentClip.xy / currentW;
    vec2 previousNdc = previousClip.xy / previousW;
    vec2 velocity = currentNdc - previousNdc;
    float speed = length(velocity);
    float maxSpeed = max(uMotionBlurMax, 0.0);
    if (speed > maxSpeed && speed > 0.0) {
        velocity *= maxSpeed / speed;
        previousNdc = currentNdc - velocity;
        previousClip.xy = previousNdc * previousClip.w;
    }

    // Trailing vertices stretch toward the previous clip position so both
    // object motion and camera motion leave a silhouette smear. Leading
    // vertices stay at the current pose.
    vec4 normalClip = uMVP * vec4(aNormal, 0.0);
    vec2 screenNormal = normalClip.xy;
    if (dot(screenNormal, velocity) < 0.0) {
        gl_Position = previousClip;
    } else {
        gl_Position = currentClip;
    }
}
