// ============================================================================

uniform int u_SamplesPerPass;


#ifdef VERTEX_SHADER
void main(void)
{
    vec2 o_TexCoord  = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1);
    gl_Position = vec4(2.0 * o_TexCoord - 1.0, 0.99999, 1.0);
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
    o_FragColor = vec4(vec3(u_SamplesPerPass * 1.0f), u_SamplesPerPass);
}
#endif // FRAGMENT_SHADER
