#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
flat in float TexLayer;
flat in vec4 UvTransform;

uniform sampler2DArray u_texture;

void main()
{
    vec2 wrapped_uv = fract(TexCoord);
    vec2 sample_uv = UvTransform.zw + wrapped_uv * UvTransform.xy;
    vec2 uv_dx = dFdx(TexCoord) * UvTransform.xy;
    vec2 uv_dy = dFdy(TexCoord) * UvTransform.xy;
    vec4 texel = textureGrad(u_texture, vec3(sample_uv, TexLayer), uv_dx, uv_dy);
    if (texel.a < 0.5) discard;
    FragColor = vec4(texel.rgb, 1.0);
}
