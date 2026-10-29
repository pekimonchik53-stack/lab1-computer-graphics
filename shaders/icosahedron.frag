#version 450

// Вход из вершинного шейдера
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;

// Выход (цвет пикселя)
layout(location = 0) out vec4 out_color;

// Uniform buffer с параметрами
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

void main() {
    // Нормализуем нормаль (после интерполяции она может быть не единичной)
    vec3 N = normalize(fragNormal);
    
    // Вектор от фрагмента к камере
    vec3 V = normalize(ubo.cameraPos - fragPos);

    // --- Ambient (рассеянный свет) ---
    vec3 ambient = ubo.ambientColor * ubo.ambientIntensity * ubo.albedo;

    // --- Diffuse (основной свет) ---
    // L - вектор НА источник света (для направленного это -lightDir)
    vec3 L = normalize(-ubo.lightDir);
    // Закон Ламберта: чем ближе нормаль к свету, тем ярче (dot(N, L))
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = ubo.lightColor * ubo.lightIntensity * diff * ubo.albedo;

    // --- Specular (блики по Блинн-Фонгу) ---
    // H - полувектор между светом и камерой
    vec3 H = normalize(L + V);
    // Чем ближе нормаль к полувектору, тем ярче блик. Возводим в степень shininess.
    float spec = pow(max(dot(N, H), 0.0), ubo.shininess);
    vec3 specular = ubo.lightColor * ubo.lightIntensity * spec * ubo.specularColor;

    // Итоговый цвет
    vec3 result = ambient + diffuse + specular;
    
    out_color = vec4(result, 1.0);
}