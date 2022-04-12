// *****************************************************************************
/**
 * Uniforms
 *
 */
uniform int u_SamplesPerPass;

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

uniform sampler2DArray u_LtcSampler;
uniform sampler3D u_LtcAnisoSampler1;
uniform sampler3D u_LtcAnisoSampler2;
uniform sampler3D u_LtcAnisoSampler3;


layout(std140, binding = BUFFER_BINDING_TRANSFORMS)
uniform Transforms {
    Transform u_Transform;
};

layout(std140, binding = BUFFER_BINDING_RANDOM)
uniform Random {
    vec4 value[64];
} u_Random;

layout(std430, binding = BUFFER_BINDING_LTC)
readonly buffer LtcBuffer {
    float u_LtcCoeffs[];
};

layout(std430, binding = BUFFER_BINDING_LTC_ANISO)
readonly buffer LtcAnisoBuffer {
    float u_LtcAnisoCoeffs[];
};

layout(std430, binding = BUFFER_BINDING_LTC_AMPLITUDE)
readonly buffer LtcAmplitudeBuffer {
    float u_LtcAmplitudeCoeffs[];
};

vec4 rand(int idx) { return u_Random.value[idx]; }
float hash(vec2 p)
{
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

uniform vec2 u_Alpha;
uniform float u_LightArea;
uniform float u_LightIntensity;
uniform mat4 u_LightVertices; // light vertices in world space

#define M_PI 3.14159265359f
float ggxNdfStd(const vec3 wm)
{
    if (wm.z > 0.0f)
        return 1.0f / M_PI;

    return 0.0f;
}

float ggxNdf(const vec3 wm, const vec2 r)
{
    const vec3 mStd = vec3(wm.x / r.x, wm.y / r.y, wm.z);
    const float nrmSqr = dot(mStd, mStd);
    const float nrmSqrSqr = nrmSqr * nrmSqr;
    const vec3 wmStd = mStd * inversesqrt(nrmSqr);

    return ggxNdfStd(wmStd) / (r.x * r.y * nrmSqrSqr);
}

float ggxSigmaStd(const vec3 wi)
{
    return (1.0f + wi.z) / 2.0f;
}

float ggxSigma(const vec3 wi, const vec2 r)
{
    const vec3 iStd = vec3(wi.x * r.x, wi.y * r.y, wi.z);
    const float nrm = inversesqrt(dot(iStd, iStd));
    const vec3 wiStd = iStd * nrm;

    return ggxSigmaStd(wiStd) / nrm;
}

float ggxPhaseFunction(vec3 wi, vec3 wo, vec2 r)
{
    const vec3 wh = normalize(wi + wo);

    return ggxNdf(wh, r) / (4.0f * ggxSigma(wi, r));
}

float ggxGc(vec3 wo, vec3 wi, vec2 r)
{
    if (wi.z > 0.0f && wo.z > 0.0f) {
        const float sigmaI = ggxSigma( wi, r);
        const float sigmaO = ggxSigma(-wo, r);
        const float tmp1 = wi.z * sigmaO;
        const float tmp2 = wo.z * sigmaI;

        return tmp2 / (tmp1 + tmp2);
    }

    return 0.0f;
}
float ggxBrdf(vec3 wi, vec3 wo, vec2 r)
{
    const float fp = ggxPhaseFunction(wi, wo, r);
    const float Gc = ggxGc(wo, wi, r);

    return fp * Gc;
}

vec3 ggxSample_Impl(vec3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
    // Section 3.2: transforming the view direction to the hemisphere configuration
    vec3 Vh = normalize(vec3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1,0,0);
    vec3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * M_PI * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
    // Section 4.3: reprojection onto hemisphere
    vec3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    return normalize(vec3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
}

vec3 ggxSample(vec2 u, vec3 wi, vec2 r)
{
    return ggxSample_Impl(wi, r.x, r.y, u.x, u.y);
}

vec3 Illumination(vec3 rayPos, vec3 rayDir)
{
    const vec3 quadVertices[4] = vec3[4](
        u_LightVertices[0].xyz,
        u_LightVertices[1].xyz,
        u_LightVertices[2].xyz,
        u_LightVertices[3].xyz
    );
    bool isInside = true;

    for (int vertexID = 0; vertexID < 4 && isInside; ++vertexID) {
        const vec3 v1 = quadVertices[vertexID          ] - rayPos;
        const vec3 v2 = quadVertices[(vertexID + 1) & 3] - rayPos;
        const vec3 v1v2 = cross(v1, v2);

        isInside = (dot(v1v2, rayDir) <= 0.0f);
    }

    if (isInside)
        return vec3(u_LightIntensity);
    else
        return vec3(0.0f);
}
//#undef M_PI

vec3 LightNormal()
{
    const vec3 quadVertices[4] = vec3[4](
        u_LightVertices[0].xyz,
        u_LightVertices[1].xyz,
        u_LightVertices[2].xyz,
        u_LightVertices[3].xyz
    );
    const vec3 x = normalize(quadVertices[1] - quadVertices[0]);
    const vec3 y = normalize(quadVertices[3] - quadVertices[0]);

    return normalize(cross(x, y));
}

vec3 SampleRect(vec2 u)
{
    const vec3 quadVertices[4] = vec3[4](
        u_LightVertices[0].xyz,
        u_LightVertices[1].xyz,
        u_LightVertices[2].xyz,
        u_LightVertices[3].xyz
    );
    const vec3 a = mix(quadVertices[0], quadVertices[1], u.x);
    const vec3 b = mix(quadVertices[3], quadVertices[2], u.x);

    return mix(a, b, u.y);
}

void ONB(vec3 n, out vec3 b1, out vec3 b2)
{
    if (n.z < -0.9999999)
    {
        b1 = vec3( 0.0, -1.0, 0.0);
        b2 = vec3(-1.0,  0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x*n.y*a;
    b1 = vec3(1.0 - n.x*n.x*a, b, -n.x);
    b2 = vec3(b, 1.0 - n.y*n.y*a, -n.y);
}

mat3 Lerp(mat3 a, mat3 b, float u)
{
    return a + u * (b - a);
}

#if 0// non-symmetrised lookup
mat3 FetchData(ivec4 P)
{
    const float m11 = u_LtcAnisoCoeffs[0 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m12 = u_LtcAnisoCoeffs[1 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m13 = u_LtcAnisoCoeffs[2 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m21 = u_LtcAnisoCoeffs[3 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m22 = u_LtcAnisoCoeffs[4 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m23 = u_LtcAnisoCoeffs[5 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m31 = u_LtcAnisoCoeffs[6 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m32 = u_LtcAnisoCoeffs[7 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m33 = u_LtcAnisoCoeffs[8 + 9 * (P.x + 32 * (P.y + 8 * (P.z + 8 * P.w)))];
    return transpose(mat3(m11, m12, m13,
                          m21, m22, m23,
                          m31, m32, m33));
}

mat3 LtcMatrix(vec4 P)
{
    const ivec4 res = ivec4(32, 8, 8, 8);
    const vec4 Ps = P * vec4(res - 1);
    const ivec4 Ps_f = min(ivec4(Ps)    , res - 1);
    const ivec4 Ps_c = min(ivec4(Ps) + 1, res - 1);
    vec4 w = fract(Ps);

    // fetch data
    const mat3 m0000 = FetchData(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m0001 = FetchData(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m0010 = FetchData(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m0011 = FetchData(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m0100 = FetchData(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m0101 = FetchData(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m0110 = FetchData(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m0111 = FetchData(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m1000 = FetchData(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m1001 = FetchData(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m1010 = FetchData(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m1011 = FetchData(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m1100 = FetchData(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m1101 = FetchData(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m1110 = FetchData(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m1111 = FetchData(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);

    // lerp first dimension
    const mat3 m000 = Lerp(m0000, m0001, w.x);
    const mat3 m001 = Lerp(m0010, m0011, w.x);
    const mat3 m010 = Lerp(m0100, m0101, w.x);
    const mat3 m011 = Lerp(m0110, m0111, w.x);
    const mat3 m100 = Lerp(m1000, m1001, w.x);
    const mat3 m101 = Lerp(m1010, m1011, w.x);
    const mat3 m110 = Lerp(m1100, m1101, w.x);
    const mat3 m111 = Lerp(m1110, m1111, w.x);

    // lerp second dimension
    const mat3 m00 = Lerp(m000, m001, w.y);
    const mat3 m01 = Lerp(m010, m011, w.y);
    const mat3 m10 = Lerp(m100, m101, w.y);
    const mat3 m11 = Lerp(m110, m111, w.y);

    // lerp third dimension
    const mat3 m0 = Lerp(m00, m01, w.z);
    const mat3 m1 = Lerp(m10, m11, w.z);

    // lerp fourth dimension and return
    return Lerp(m0, m1, w.w);
}
#else // symmetrised data
mat3 FetchData_Matrix(ivec4 P)
{
    const float m11 = u_LtcAnisoCoeffs[0 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m12 = u_LtcAnisoCoeffs[1 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m13 = u_LtcAnisoCoeffs[2 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m21 = u_LtcAnisoCoeffs[3 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m22 = u_LtcAnisoCoeffs[4 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m23 = u_LtcAnisoCoeffs[5 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m31 = u_LtcAnisoCoeffs[6 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m32 = u_LtcAnisoCoeffs[7 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    const float m33 = u_LtcAnisoCoeffs[8 + 9 * (P.x + 8 * (P.y + 8 * (P.z + 8 * P.w)))];
    return transpose(mat3(m11, m12, m13,
                          m21, m22, m23,
                          m31, m32, m33));
}


mat3 LtcMatrix(vec4 P)
{
    const vec4 Ps = P * vec4(7);
    const ivec4 Ps_f = min(ivec4(Ps)    , 7);
    const ivec4 Ps_c = min(ivec4(Ps) + 1, 7);
    const vec4 w = fract(Ps);

    // fetch data
    const mat3 m0000 = FetchData_Matrix(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m0001 = FetchData_Matrix(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m0010 = FetchData_Matrix(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m0011 = FetchData_Matrix(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m0100 = FetchData_Matrix(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m0101 = FetchData_Matrix(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m0110 = FetchData_Matrix(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m0111 = FetchData_Matrix(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m1000 = FetchData_Matrix(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m1001 = FetchData_Matrix(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m1010 = FetchData_Matrix(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m1011 = FetchData_Matrix(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const mat3 m1100 = FetchData_Matrix(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const mat3 m1101 = FetchData_Matrix(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const mat3 m1110 = FetchData_Matrix(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const mat3 m1111 = FetchData_Matrix(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);

    // lerp first dimension
    const mat3 m000 = Lerp(m0000, m0001, w.x);
    const mat3 m001 = Lerp(m0010, m0011, w.x);
    const mat3 m010 = Lerp(m0100, m0101, w.x);
    const mat3 m011 = Lerp(m0110, m0111, w.x);
    const mat3 m100 = Lerp(m1000, m1001, w.x);
    const mat3 m101 = Lerp(m1010, m1011, w.x);
    const mat3 m110 = Lerp(m1100, m1101, w.x);
    const mat3 m111 = Lerp(m1110, m1111, w.x);

    // lerp second dimension
    const mat3 m00 = Lerp(m000, m001, w.y);
    const mat3 m01 = Lerp(m010, m011, w.y);
    const mat3 m10 = Lerp(m100, m101, w.y);
    const mat3 m11 = Lerp(m110, m111, w.y);

    // lerp third dimension
    const mat3 m0 = Lerp(m00, m01, w.z);
    const mat3 m1 = Lerp(m10, m11, w.z);

    // lerp fourth dimension and return
    return Lerp(m0, m1, w.w);
}
#endif

float FetchData_Amplitude(ivec4 P)
{
    return u_LtcAmplitudeCoeffs[P.x + 8 * (P.y + 8 * (P.z + 8 * P.w))];
}

float LtcAmplitude(vec4 P)
{
    const ivec4 res = ivec4(8);
    const vec4 Ps = P * vec4(res - 1);
    const ivec4 Ps_f = min(ivec4(Ps)    , res - 1);
    const ivec4 Ps_c = min(ivec4(Ps) + 1, res - 1);
    const vec4 w = fract(Ps);

    // fetch data
    const float m0000 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const float m0001 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const float m0010 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const float m0011 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const float m0100 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const float m0101 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const float m0110 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const float m0111 = FetchData_Amplitude(ivec4(Ps_f.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);
    const float m1000 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_f.x).wzyx);
    const float m1001 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_f.z, Ps_f.y, Ps_c.x).wzyx);
    const float m1010 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_f.x).wzyx);
    const float m1011 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_f.z, Ps_c.y, Ps_c.x).wzyx);
    const float m1100 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_f.x).wzyx);
    const float m1101 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_c.z, Ps_f.y, Ps_c.x).wzyx);
    const float m1110 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_f.x).wzyx);
    const float m1111 = FetchData_Amplitude(ivec4(Ps_c.w, Ps_c.z, Ps_c.y, Ps_c.x).wzyx);

    // lerp first dimension
    const float m000 = mix(m0000, m0001, w.x);
    const float m001 = mix(m0010, m0011, w.x);
    const float m010 = mix(m0100, m0101, w.x);
    const float m011 = mix(m0110, m0111, w.x);
    const float m100 = mix(m1000, m1001, w.x);
    const float m101 = mix(m1010, m1011, w.x);
    const float m110 = mix(m1100, m1101, w.x);
    const float m111 = mix(m1110, m1111, w.x);

    // lerp second dimension
    const float m00 = mix(m000, m001, w.y);
    const float m01 = mix(m010, m011, w.y);
    const float m10 = mix(m100, m101, w.y);
    const float m11 = mix(m110, m111, w.y);

    // lerp third dimension
    const float m0 = mix(m00, m01, w.z);
    const float m1 = mix(m10, m11, w.z);

    // lerp fourth dimension and return
    return mix(m0, m1, w.w) / (2.0f * M_PI);
}


mat3 FetchData_Tex3D(vec3 P)
{
    const vec3 m0 = texture(u_LtcAnisoSampler1, P).rgb;
    const vec3 m1 = texture(u_LtcAnisoSampler2, P).rgb;
    const vec3 m2 = texture(u_LtcAnisoSampler3, P).rgb;

    return transpose(mat3(m0, m1, m2));
}

mat3 LtcMatrix_Tex3D(vec4 P)
{
    const float ws = P.w * 7.0f;
    const float ws_f = floor(ws       );
    const float ws_c = min(floor(ws + 1.0f), 7.0f);
    const float w = fract(ws);

#if 1
    const float x = (P.x * 7.0 + 0.5) / 8.0;
#else
    const float x = (P.x * 31.0 + 0.5) / 32.0;
#endif
    const float y = (P.y * 7.0 + 0.5) / 8.0;
    const float z1 = ((P.z * 7.0 + 8.0 * ws_f + 0.5) / 64.0);
    const float z2 = ((P.z * 7.0 + 8.0 * ws_c + 0.5) / 64.0);

    const mat3 m1 = FetchData_Tex3D(vec3(x, y, z1));
    const mat3 m2 = FetchData_Tex3D(vec3(x, y, z2));

    return Lerp(m1, m2, w);
}


void FetchData_Tex3D(vec3 P, out mat3 ltcMatrix, out vec2 ltcNrm)
{
    const vec4 t0 = texture(u_LtcAnisoSampler1, P);
    const vec4 t1 = texture(u_LtcAnisoSampler2, P);
    const vec4 t2 = texture(u_LtcAnisoSampler3, P);
    const vec3 m0 = t0.rgb;
    const vec3 m1 = vec3(t0.a, t1.rg);
    const vec3 m2 = vec3(t1.ba, t2.r);

    ltcMatrix = transpose(mat3(m0, m1, m2));
    ltcNrm = t2.gb;
}

void LtcData_Tex3D(vec4 P, out mat3 ltcMatrix, out vec2 ltcNrm)
{
    const float ws = P.w * 7.0f;
    const float ws_f = floor(ws       );
    const float ws_c = min(floor(ws + 1.0f), 7.0f);
    const float w = fract(ws);

    const float x = (P.x * 7.0 + 0.5) / 8.0;
    const float y = (P.y * 7.0 + 0.5) / 8.0;
    const float z1 = ((P.z * 7.0 + 8.0 * ws_f + 0.5) / 64.0);
    const float z2 = ((P.z * 7.0 + 8.0 * ws_c + 0.5) / 64.0);

    mat3 m1; vec2 nrm1; FetchData_Tex3D(vec3(x, y, z1), m1, nrm1);
    mat3 m2; vec2 nrm2; FetchData_Tex3D(vec3(x, y, z2), m2, nrm2);

    ltcMatrix = Lerp(m1, m2, w);
    ltcNrm = mix(nrm1, nrm2, w);
}

float IntegrateEdge(vec3 v1, vec3 v2)
{
    float cosTheta = dot(v1, v2);
    float theta = acos(cosTheta);
    float res = cross(v1, v2).z * ((theta > 0.001) ? theta/sin(theta) : 1.0);

    return res;
}

void ClipQuadToHorizon(inout vec3 L[5], out int n)
{
    // detect clipping config
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    // clip
    n = 0;

    if (config == 0)
    {
        // clip all
    }
    else if (config == 1) // V1 clip V2 V3 V4
    {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) // V2 clip V1 V3 V4
    {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) // V1 V2 clip V3 V4
    {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) // V3 clip V1 V2 V4
    {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5) // V1 V3 clip V2 V4) impossible
    {
        n = 0;
    }
    else if (config == 6) // V2 V3 clip V1 V4
    {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) // V1 V2 V3 clip V4
    {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) // V4 clip V1 V2 V3
    {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    }
    else if (config == 9) // V1 V4 clip V2 V3
    {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10) // V2 V4 clip V1 V3) impossible
    {
        n = 0;
    }
    else if (config == 11) // V1 V2 V4 clip V3
    {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) // V3 V4 clip V1 V2
    {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) // V1 V3 V4 clip V2
    {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) // V2 V3 V4 clip V1
    {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15) // V1 V2 V3 V4
    {
        n = 4;
    }

    if (n == 3)
        L[3] = L[0];
    if (n == 4)
        L[4] = L[0];
}


float
LTC_Integrate(vec3 P, mat3 TBN, mat3 M, mat4x3 L, bool twoSided)
{
    // polygon (allocate 5 vertices for clipping)
    vec3 V[5];
    V[0] = M * (TBN * (L[0] - P));
    V[1] = M * (TBN * (L[1] - P));
    V[2] = M * (TBN * (L[2] - P));
    V[3] = M * (TBN * (L[3] - P));

    int n;
    ClipQuadToHorizon(V, n);

    if (n == 0)
        return 0.0f;

    // project onto sphere
    V[0] = normalize(V[0]);
    V[1] = normalize(V[1]);
    V[2] = normalize(V[2]);
    V[3] = normalize(V[3]);
    V[4] = normalize(V[4]);

    // integrate
    float sum = 0.0;

    sum += IntegrateEdge(V[0], V[1]);
    sum += IntegrateEdge(V[1], V[2]);
    sum += IntegrateEdge(V[2], V[3]);
    if (n >= 4)
        sum += IntegrateEdge(V[3], V[4]);
    if (n == 5)
        sum += IntegrateEdge(V[4], V[0]);

    sum = twoSided ? abs(sum) : max(0.0, sum);

    return sum;
}

void
LtcMatrixAndNormalization(
    vec3 V,
    mat3 TBN,
    vec2 R,
    inout mat4x3 L,
    out mat3 ltcMatrix,
    out float ltcNormalization
) {
    const vec3 wo = TBN * V;
    const float thetaO = acos(wo.z);
    const bool flipConfig = R.y > R.x;
    const float phiO_tmp = atan(wo.y, wo.x);
    const float phiO_tmp2 = flipConfig ? (M_PI / 2.0f - phiO_tmp) : phiO_tmp;
    const float phiO = phiO_tmp2 >= 0.0f ? phiO_tmp2 : phiO_tmp2 + 2.0 * M_PI;
    const float u0 = ((flipConfig ? R.y : R.x) - 1e-3) / (1.0f - 1e-3);
    const float u1 = (flipConfig ? R.x / R.y : R.y / R.x);
    const float u2 = thetaO / M_PI * 2.0f;
    const mat4x3 L_hack = L;

#ifndef INTEGRATOR_LTC_ANALYTIC_ISOTROPIC
    if (phiO < M_PI * 0.5f) {
        const float u3 = phiO / (M_PI * 0.5f);
        const vec4 u = vec4(u3 / 1.0f, u2, u1, u0);
        const mat4 winding = transpose(mat4(0, 0, 0, 1,
                                            0, 0, 1, 0,
                                            0, 1, 0, 0,
                                            1, 0, 0, 0));

        L = transpose(winding * transpose(L));
        ltcMatrix = LtcMatrix_Tex3D(u);
        ltcNormalization = LtcAmplitude(u);

    } else if (phiO >= M_PI * 0.5f && phiO < M_PI) {
        const float u3 = (M_PI - phiO) / (M_PI * 0.5f);
        const vec4 u = vec4(u3 / 1.0f, u2, u1, u0);
        const mat3 flip = mat3(-1.0, 0.0, 0.0,
                                0.0, 1.0, 0.0,
                                0.0, 0.0, 1.0);
        ltcMatrix = flip * LtcMatrix_Tex3D(u);
        ltcNormalization = LtcAmplitude(u);

    } else if (phiO >= M_PI && phiO < 1.5f * M_PI) {
        const float u3 = (phiO - M_PI) / (M_PI * 0.5f);
        const vec4 u = vec4(u3 / 1.0f, u2, u1, u0);
        const mat3 flip = mat3(-1.0, 0.0, 0.0,
                                0.0, -1.0, 0.0,
                                0.0, 0.0, 1.0);
        const mat4 winding = transpose(mat4(0, 0, 0, 1,
                                            0, 0, 1, 0,
                                            0, 1, 0, 0,
                                            1, 0, 0, 0));

        L = transpose(winding * transpose(L));
        ltcMatrix = flip * LtcMatrix_Tex3D(u);
        ltcNormalization = LtcAmplitude(u);

    } else if (phiO >= 1.5f * M_PI && phiO < 2.0f * M_PI) {
        const float u3 = (2.0f * M_PI - phiO) / (M_PI * 0.5f);
        const vec4 u = vec4(u3 / 1.0f, u2, u1, u0);
        const mat3 flip = mat3(1.0, 0.0, 0.0,
                               0.0, -1.0, 0.0,
                               0.0, 0.0, 1.0);

        ltcMatrix = flip * LtcMatrix_Tex3D(u);
        ltcNormalization = LtcAmplitude(u);
    }

    if (flipConfig) {
        const mat3 rotMatrix = mat3(0, +1, 0,
                                    +1, 0, 0,
                                    0,  0, 1);
        const mat4 winding = transpose(mat4(0, 0, 0, 1,
                                            0, 0, 1, 0,
                                            0, 1, 0, 0,
                                            1, 0, 0, 0));

        L = transpose(winding * transpose(L));
        ltcMatrix = rotMatrix * ltcMatrix;
    }
#endif
#if INTEGRATOR_LTC_ANALYTIC_ISOTROPIC
    const float res = 64.0f;
    vec2 p = (vec2(u0, u2) * (res - 1.0f) + 0.5f) / res;
    const vec3 c1 = texture(u_LtcSampler, vec3(p, 0.0)).rgb;
    const vec3 c2 = texture(u_LtcSampler, vec3(p, 1.0)).rgb;
    const vec3 c3 = texture(u_LtcSampler, vec3(p, 2.0)).rgb;
    const float cO = cos(phiO);
    const float sO = sin(phiO);
    const mat3 mRot = mat3(cO, +sO, 0,
                           -sO, cO, 0,
                           0, 0, 1);

    ltcMatrix = transpose(mat3(c1, c2, vec3(0, 0, 1))) * transpose(mRot);
    L = L_hack;
#endif
}

#define M00(m) m[0][0]
#define M01(m) m[0][1]
#define M02(m) m[0][2]
#define M03(m) m[0][3]
#define M10(m) m[1][0]
#define M11(m) m[1][1]
#define M12(m) m[1][2]
#define M13(m) m[1][3]
#define M20(m) m[2][0]
#define M21(m) m[2][1]
#define M22(m) m[2][2]
#define M23(m) m[2][3]
#define M30(m) m[3][0]
#define M31(m) m[3][1]
#define M32(m) m[3][2]
#define M33(m) m[3][3]

mat3 adjugate(mat3 m)
{
    mat3 a;

    M00(a) = M11(m) * M22(m) - M12(m) * M21(m);
    M01(a) =-M01(m) * M22(m) + M02(m) * M21(m);
    M02(a) = M01(m) * M12(m) - M02(m) * M11(m);

    M10(a) =-M10(m) * M22(m) + M12(m) * M20(m);
    M11(a) = M00(m) * M22(m) - M02(m) * M20(m);
    M12(a) =-M00(m) * M12(m) + M02(m) * M10(m);

    M20(a) = M10(m) * M21(m) - M11(m) * M20(m);
    M21(a) =-M00(m) * M21(m) + M01(m) * M20(m);
    M22(a) = M00(m) * M11(m) - M01(m) * M10(m);

    return a;
}

mat3 myInverse(mat3 m)
{
    float det = determinant(m);

    return (1.0f / det) * adjugate(m);
}


// V: world-space fragment position
// V: world-space view direction
// T: world-space tangent direction
// B: world-space bitangent direction
// N: world-space normal direction
// R: GGX roughness
// L: world-space area-light vertices
float GGX_AreaLight(vec3 P, vec3 V, vec3 T, vec3 B, vec3 N, vec2 R, mat4x3 L)
{
    const mat3 TBN = transpose(mat3(T, B, N));
    mat3 ltcMatrix;
    float ltcNormalization;
    LtcMatrixAndNormalization(V, TBN, R, L, ltcMatrix, ltcNormalization);
    ltcMatrix = inverse(ltcMatrix);
    float ltcIntegral = LTC_Integrate(P, TBN, ltcMatrix, L, false);

    return ltcIntegral * ltcNormalization;
}

// ============================================================================

// *****************************************************************************
/**
 * Vertex Shader
 *
 * The shader outputs attributes relevant for shading in view space.
 */
#ifdef VERTEX_SHADER
layout(location = 0) in vec4 i_Position;
layout(location = 1) in vec4 i_TexCoord;
layout(location = 2) in vec4 i_Tangent1;
layout(location = 3) in vec4 i_Tangent2;
layout(location = 0) out vec4 o_Position;
layout(location = 1) out vec4 o_TexCoord;
layout(location = 2) out vec4 o_Tangent1;
layout(location = 3) out vec4 o_Tangent2;

void main(void)
{
    vec4 p = i_Position;
    o_Position = u_Transform.model * p;
    o_TexCoord = i_TexCoord;
    o_Tangent1 = u_Transform.model * i_Tangent1;
    o_Tangent2 = u_Transform.model * i_Tangent2;

    gl_Position = u_Transform.modelViewProjection * p;
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec4 i_Position;
layout(location = 1) in vec4 i_TexCoord;
layout(location = 2) in vec4 i_Tangent1;
layout(location = 3) in vec4 i_Tangent2;
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
    //o_FragColor = vec4(1, 0, 1, 1); return;
#if 1
    // extract attributes
#if 1 // true frame
    vec3 wx = normalize(i_Tangent1.xyz);
    vec3 wy = normalize(i_Tangent2.xyz);
    vec3 wn = normalize(cross(wx, wy));
    //wy = cross(wx, wn);
#else // enforce orthonormal frame
    vec3 wn = normalize(i_Position.xyz);
    vec3 wy = normalize(cross(wn, vec3(0, 1, 0)));
    vec3 wx = normalize(cross(wy, wn));
#endif
    vec3 wo = normalize(u_Transform.viewInv[3].xyz - i_Position.xyz);
    mat3 tgInv = mat3(wx, wy, wn);
    mat3 tg = transpose(tgInv);

    // express data in tangent space
    wo = tg * wo;
    wn = tg * wn;//vec3(0, 0, 1);
    //wn = vec3(0, 0, 1);

    // initialize emitted and outgoing radiance
    vec3 Lo = vec3(0);

    vec3 worldPos = (i_Position).xyz;


// -----------------------------------------------------------------------------
/**
 * Shading with Importance Sampling
 *
 */
#if INTEGRATOR_GGX
    // loop over all samples
    for (int sampleID = 0; sampleID < u_SamplesPerPass; ++sampleID) {
        const float h1 = hash(gl_FragCoord.xy);
        const float h2 = hash(gl_FragCoord.yx);
        const vec2 u = mod(vec2(h1, h2) + rand(sampleID).xy, vec2(1.0));
        const vec3 wm = ggxSample(u, wo, u_Alpha);
        const vec3 wi = normalize(2.0f * dot(wm, wo) * wm - wo);
        const vec3 wiWorld = normalize(tgInv * wi);
        const vec3 Li = Illumination(worldPos, wiWorld);

        Lo+= Li * ggxGc(wi, wo, u_Alpha);
    }

    o_FragColor = vec4(Lo, u_SamplesPerPass);

// -----------------------------------------------------------------------------
/**
 * Shading with Importance Sampling
 *
 */
#elif INTEGRATOR_GGX_LIGHT
        // loop over all samples
    for (int sampleID = 0; sampleID < u_SamplesPerPass; ++sampleID) {
        const float h1 = hash(gl_FragCoord.xy);
        const float h2 = hash(gl_FragCoord.yx);
        const vec2 u = mod(vec2(h1, h2) + rand(sampleID).xy, vec2(1.0));
        const vec3 rayPos = SampleRect(u);
        const vec3 toLight = rayPos - worldPos;
        const vec3 rayDir = normalize(toLight);
        const vec3 Li = Illumination(rayPos, rayDir);
        const vec3 wi = normalize( tg * rayDir );
        const float weight = clamp(dot(LightNormal(), -rayDir), 0.0f, 1.0f)
                           / dot(toLight, toLight);

        Lo+= Li * ggxBrdf(wo, wi, u_Alpha) * weight * u_LightArea;
    }

    o_FragColor = vec4(Lo, u_SamplesPerPass);

// -----------------------------------------------------------------------------
/**
 * Shading with MIS
 *
 */
#elif INTEGRATOR_GGX_MIS
    // loop over all samples
    for (int j = 0; j < u_SamplesPerPass; ++j) {
        // compute a uniform sample
        const float h1 = hash(gl_FragCoord.xy);
        const float h2 = hash(gl_FragCoord.yx);
        const vec2 u = mod(vec2(h1, h2) + rand(j).xy, vec2(1.0));

        // importance sample GGX
        if (true) {
            const vec3 wm = ggxSample(u, wo, u_Alpha);
            const vec3 wi = normalize(2.0f * dot(wm, wo) * wm - wo);
            const vec3 wiWorld = normalize(transpose(tg) * wi);
            const vec3 Li = Illumination(worldPos, wiWorld);
            const float pdf1 = ggxPhaseFunction(wo, wi, u_Alpha);
            const float weight1 = ggxGc(wi, wo, u_Alpha);

            if (pdf1 > 0.0) {
                const float pdf2 = 1.0f / u_LightArea;
                const float misWeight = pdf1 * pdf1;
                const float misNrm = pdf1 * pdf1 + pdf2 * pdf2;

                Lo+= Li * weight1 * misWeight / misNrm;
            }
        }

        // importance sample light source
        if (true) {
            const vec3 rayPos = SampleRect(u);
            const vec3 toLight = rayPos - worldPos;
            const vec3 rayDir = normalize(toLight);
            const vec3 Li = Illumination(rayPos, rayDir);
            const vec3 wi = normalize( tg * rayDir );
            const float pdf2 = 1.0f / u_LightArea;
            const float weight2 = clamp(dot(LightNormal(), -rayDir), 0.0f, 1.0f)
                               / dot(toLight, toLight) * ggxBrdf(wo, wi, u_Alpha) / pdf2;

            if (pdf2 > 0.0) {
                float pdf1 = ggxPhaseFunction(wo, wi, u_Alpha);
                float misWeight = pdf2 * pdf2;
                float misNrm = pdf1 * pdf1 + pdf2 * pdf2;

                Lo+= Li * weight2 * misWeight / misNrm;
            }
        }
    }

    o_FragColor = vec4(Lo, u_SamplesPerPass);

// -----------------------------------------------------------------------------
/**
 * Shading with LTC
 *
 */
#elif defined(INTEGRATOR_LTC) || defined(INTEGRATOR_LTC_ANALYTIC) || defined(INTEGRATOR_LTC_ANALYTIC_ISOTROPIC)
    const vec2 R = u_Alpha;
    mat4x3 L = mat4x3(u_LightVertices[0].xyz,
                      u_LightVertices[1].xyz,
                      u_LightVertices[2].xyz,
                      u_LightVertices[3].xyz);
    float nrm;
    mat3 mLtc;
    LtcMatrixAndNormalization(tgInv * wo,
                              tg,
                              R,
                              L,
                              mLtc,
                              nrm);
#if INTEGRATOR_LTC
    for (int sampleID = 0; sampleID < u_SamplesPerPass; ++sampleID) {
        const float h1 = hash(gl_FragCoord.xy);
        const float h2 = hash(gl_FragCoord.yx);
        const vec2 u = mod(vec2(h1, h2) + rand(sampleID).xy, vec2(1.0));
        const float r = sqrt(u.x);
        const float x = r * cos(2.0f * M_PI * u.y);
        const float y = r * sin(2.0f * M_PI * u.y);
        const float z = sqrt(1.0f - u.x);
        const vec3 wiStd = vec3(x, y, z);
        const vec3 wi = normalize(mLtc * wiStd);
        const vec3 wiWorld = normalize(tgInv * wi);
        const vec3 Li = Illumination(worldPos, wiWorld);

        Lo+= Li;
    }

#elif defined(INTEGRATOR_LTC_ANALYTIC) || defined(INTEGRATOR_LTC_ANALYTIC_ISOTROPIC)
#if INTEGRATOR_LTC_ANALYTIC
    L = mat4x3(u_LightVertices[0].xyz,
               u_LightVertices[1].xyz,
               u_LightVertices[2].xyz,
               u_LightVertices[3].xyz);
#else
    L = mat4x3(u_LightVertices[0].xyz,
               u_LightVertices[3].xyz,
               u_LightVertices[2].xyz,
               u_LightVertices[1].xyz);
#endif

    Lo = GGX_AreaLight(worldPos,
                       tgInv * wo,
                       wx,
                       wy,
                       tgInv * wn,
                       u_Alpha,
                       L)
       * vec3(u_LightIntensity) * u_SamplesPerPass;
#endif

    o_FragColor = vec4(Lo, u_SamplesPerPass);


// -----------------------------------------------------------------------------
/**
 * Debug Shading
 *
 * Do whatever you like in here.
 */
#else
    o_FragColor = vec4(0, 1, 0, 1); return;

// -----------------------------------------------------------------------------
#endif // SHADE
#endif
}
#endif // FRAGMENT_SHADER


