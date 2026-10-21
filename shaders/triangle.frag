#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
flat in float TexLayer;

uniform sampler2DArray u_texture;

void main()
{
    FragColor = texture(u_texture, vec3(TexCoord, TexLayer));
}
