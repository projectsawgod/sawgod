// Mode 7 focus (Tilt-Shift): sharp in the middle; in a band along every screen edge the picture softens toward
// that edge. The blur depends only on the distance in from the nearest edge, in native pixels, so equal blur lies
// on rectangles concentric with the screen, not on an oval. Drawn on the finished world picture
// before the UI is restored on top, so text and windows never blur.
// SDL GPU renderer fragment ABI: color at location 0, UV at location 1, source texture/sampler at slot 0 in
// fragment resource space 2; uniforms in space 3.
Texture2D<float4> picture : register(t0, space2);
SamplerState picture_sampler : register(s0, space2);

cbuffer Params : register(b0, space3) {
    float4 band;    // xy: the soft band's depth in from the edges, in UV (one native depth in x and y); zw unused
    float4 control; // x: blur radius in UV (y axis) at the screen edge, y: width/height ratio
};

static const float2 kDisc[12] = {
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
    float2(0.962, -0.195),  float2(0.473, -0.480),  float2(0.519, 0.767),  float2(0.185, -0.893),
    float2(0.507, 0.064),   float2(0.896, 0.412),   float2(-0.322, -0.933), float2(-0.792, -0.598)};

// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    // Sharp inside the inner rectangle; across the band the blur eases up to full strength at the screen edge.
    const float2 inside = min(uv, 1 - uv) / max(band.xy, 1e-5);
    const float t = saturate(1 - min(inside.x, inside.y));
    const float amount = t * t;
    uint w, h;
    picture.GetDimensions(w, h);
    // The sharp center reads exact texels (the draw may scale); only the blur taps filter.
    float3 rgb = picture.Load(int3(min(uint2(uv * float2(w, h)), uint2(w - 1, h - 1)), 0)).rgb;
    if (amount > 0.001) {
        const float2 radius = float2(control.x / control.y, control.x) * amount;
        float3 sum = rgb;
        [unroll] for (int i = 0; i < 12; ++i)
            sum += picture.Sample(picture_sampler, uv + kDisc[i] * radius).rgb;
        rgb = sum / 13.0;
    }
    return float4(rgb, 1.0) * color;
}
