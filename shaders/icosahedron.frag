#version 450

// Вход: цвет вершины (интерполированный между вершинами треугольника)
layout(location = 0) in vec3 frag_color;

// Выход: цвет пикселя
layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(frag_color, 1.0);
}