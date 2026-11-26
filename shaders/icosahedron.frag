#version 450
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragLightPos;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 proj;
    vec3 cameraPos; vec3 lightDir; vec3 lightColor; float lightIntensity;
    vec3 albedo; vec3 specularColor; float shininess;
    vec3 ambientColor; float ambientIntensity;
    mat4 lightViewProj;
} ubo;

// НОВОЕ: Текстура
layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;

layout(location = 0) out vec4 out_color;

float getShadow() {
    vec3 p = fragLightPos.xyz / fragLightPos.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    // X/Y in -1..1, Vulkan Z in 0..1; geometry outside the map is sunlit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z < 0.0 || p.z > 1.0)
        return 1.0;
    return texture(shadowMap, vec3(uv, p.z - 0.001));
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragPos);

    // Берем цвет из текстуры
    vec4 texColor = texture(texSampler, fragUV);
    vec3 albedo = texColor.rgb * ubo.albedo; // Умножаем на базовый цвет (чтобы можно было тонировать)

    // Make the floor recognizable without an additional texture dependency.
    if (fragPos.y < -1.8) {
        vec2 tile = floor(fragPos.xz * 0.75);
        float checker = mod(tile.x + tile.y, 2.0);
        albedo = mix(vec3(0.30, 0.36, 0.40), vec3(0.57, 0.62, 0.66), checker);
    }

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

    vec3 result = ambient + getShadow() * (diffuse + specular);
    out_color = vec4(result, 1.0);
}