#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv; // Новый вход: UV

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 proj;
    vec3 cameraPos; vec3 lightDir; vec3 lightColor; float lightIntensity;
    vec3 albedo; vec3 specularColor; float shininess;
    vec3 ambientColor; float ambientIntensity;
} ubo;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV; // Передаем UV дальше

void main() {
    vec4 worldPos = ubo.model * vec4(in_position, 1.0);
    fragPos = worldPos.xyz;
    fragNormal = mat3(ubo.model) * in_normal;
    fragUV = in_uv;
    gl_Position = ubo.proj * ubo.view * worldPos;
}