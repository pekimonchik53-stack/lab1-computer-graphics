#version 450
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 proj;
    vec3 cameraPos; vec3 lightDir; vec3 lightColor; float lightIntensity;
    vec3 albedo; vec3 specularColor; float shininess;
    vec3 ambientColor; float ambientIntensity;
} ubo;

// НОВОЕ: Текстура
layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragPos);

    // Берем цвет из текстуры
    vec4 texColor = texture(texSampler, fragUV);
    vec3 albedo = texColor.rgb * ubo.albedo; // Умножаем на базовый цвет (чтобы можно было тонировать)

    // Ambient
    vec3 ambient = ubo.ambientColor * ubo.ambientIntensity * albedo;

    // Diffuse
    vec3 L = normalize(-ubo.lightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = ubo.lightColor * ubo.lightIntensity * diff * albedo;

    // Specular
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), ubo.shininess);
    vec3 specular = ubo.lightColor * ubo.lightIntensity * spec * ubo.specularColor;

    vec3 result = ambient + diffuse + specular;
    out_color = vec4(result, 1.0);
}