#version 450

// Вход: позиция и нормаль (из vertex buffer)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

// Uniform buffer: общие данные сцены
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    vec3 lightDir;
    vec3 lightColor;
    float lightIntensity;
    vec3 albedo;
    vec3 specularColor;
    float shininess;
    vec3 ambientColor;
    float ambientIntensity;
} ubo;

// Выход во фрагментный шейдер
layout(location = 0) out vec3 fragPos;   // Позиция в мире
layout(location = 1) out vec3 fragNormal;// Нормаль в мире

void main() {
    // Переводим вершину в мировые координаты
    vec4 worldPos = ubo.model * vec4(in_position, 1.0);
    fragPos = worldPos.xyz;

    // Переводим нормаль в мировые координаты
    // (mat3(ubo.model) берет только верхнюю левую часть матрицы для поворота)
    fragNormal = mat3(ubo.model) * in_normal;

    // Стандартная проекция
    gl_Position = ubo.proj * ubo.view * worldPos;
}