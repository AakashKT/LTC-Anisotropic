uniform int u_SamplesPerPass;
uniform float u_LightIntensity;
uniform mat4 u_LightVertices; // light vertices in world space

struct Transform {
    mat4 modelView;
    mat4 modelViewInv;
    mat4 modelViewProjection;
    mat4 viewInv;
    mat4 view;
    mat4 model;
    mat4 viewProjection;
    mat4 dummy3;
};

layout(std140, binding = BUFFER_BINDING_TRANSFORMS)
uniform Transforms {
    Transform u_Transform;
};


#ifdef VERTEX_SHADER
void main(void)
{
    const uvec4 vertexIDs = uvec4(0, 1, 3, 2);
    const uint vertexID = vertexIDs[gl_VertexID];
    const vec4 lightVertex = u_LightVertices[vertexID];

    gl_Position = u_Transform.viewProjection * lightVertex;
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
    vec3 Lo = vec3(u_LightIntensity);

    o_FragColor = vec4(Lo, 1.0) * u_SamplesPerPass;
}
#endif
