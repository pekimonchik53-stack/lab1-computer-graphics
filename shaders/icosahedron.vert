#version 450

// Вход: позиция вершины и её цвет (из vertex buffer)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

// Uniform buffer: матрицы и цвет фигуры
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color_multiplier;  // цвет из ImGUI (умножается на цвет вершины)
} ubo;

// Выход: цвет вершины, передаётся во фрагментный шейдер
layout(location = 0) out vec3 frag_color;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(in_position, 1.0);
    frag_color = in_color * ubo.color_multiplier.rgb;
}