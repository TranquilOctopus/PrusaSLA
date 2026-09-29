#version 140

uniform vec4 uniform_color;

// x = tainted, y = specular;
in vec2 intensity;
in vec2 clipping_planes_dots;

out vec4 out_color;

void main()
{
    if (any(lessThan(clipping_planes_dots, vec2(0.0))))
        discard;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}