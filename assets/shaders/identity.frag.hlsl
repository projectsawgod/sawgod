// SDL GPU renderer fragment ABI: color at location 0, UV at location 1,
// source texture/sampler at slot 0 in fragment resource space 2.
// XRGB game textures are opaque, even when their unused upload byte is zero.
Texture2D<float4> picture : register(t0, space2);
SamplerState picture_sampler : register(s0, space2);

// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    return float4(picture.Sample(picture_sampler, uv).rgb, 1.0) * color;
}
