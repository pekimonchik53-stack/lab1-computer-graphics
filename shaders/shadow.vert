#version 450
layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 proj;
    vec3 cameraPos; vec3 lightDir; vec3 lightColor; float lightIntensity;
    vec3 albedo; vec3 specularColor; float shininess;
    vec3 ambientColor; float ambientIntensity;
    mat4 lightViewProj;
} ubo;

void main() {
    gl_Position = ubo.lightViewProj * ubo.model * vec4(in_position, 1.0);
}
