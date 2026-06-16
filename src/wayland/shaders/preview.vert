#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4 matrix;
    vec4 params;
} ubuf;

void main()
{
    gl_Position = ubuf.matrix * vec4(position, 0.0, 1.0);
    int transform = int(ubuf.params.z + 0.5);
    if (transform == 1)
        v_uv = vec2(uv.y, 1.0 - uv.x);
    else if (transform == 2)
        v_uv = vec2(1.0 - uv.x, 1.0 - uv.y);
    else if (transform == 3)
        v_uv = vec2(1.0 - uv.y, uv.x);
    else if (transform == 4)
        v_uv = vec2(1.0 - uv.x, uv.y);
    else if (transform == 5)
        v_uv = vec2(1.0 - uv.y, 1.0 - uv.x);
    else if (transform == 6)
        v_uv = vec2(uv.x, 1.0 - uv.y);
    else if (transform == 7)
        v_uv = vec2(uv.y, uv.x);
    else
        v_uv = uv;
}
