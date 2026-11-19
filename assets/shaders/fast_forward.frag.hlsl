// Variant A (WSC/Enhanced) and D (GB), after the completed camera/scene and before host overlays.
// The ownership test returns exact clean pixels for live native/host panes.
Texture2D<float4> picture : register(t0, space2);
SamplerState picture_sampler : register(s0, space2);
Texture2D<float4> previous : register(t1, space2);
SamplerState previous_sampler : register(s1, space2);
Texture2D<float4> ownership : register(t2, space2);
SamplerState ownership_sampler : register(s2, space2);
cbuffer Params : register(b0, space3) {
    float4 game; // output pixel rect
    float4 control; // strength, GB weight, presentation seconds
    float4 output_size;
};
// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    int2 p = min(int2(uv * output_size.xy), int2(output_size.xy)-1);
    float3 raw = picture.Load(int3(p, 0)).rgb;
    float2 native = (float2(p)+0.5-game.xy) * float2(224,144) / game.zw;
    if (any(native < 0) || any(native >= float2(224,144))) return float4(raw,1)*color;
    if (ownership.Load(int3(int2(native),0)).r > 0) return float4(raw,1)*color;
    float s = control.x, gb = control.y, t = control.z;
    float band = max(0,1-fmod(native.y+t*24,144)/2)
               + max(0,1-fmod(native.y+t*24+47,144)/2)
               + max(0,1-fmod(native.y+t*24+99,144)/2);
    int dx = int(round(band*sin(t*21)*s*game.z/224));
    int sx = clamp(p.x+dx, int(max(0,game.x)), min(int(output_size.x)-1,int(ceil(game.x+game.z))-1));
    float3 a = picture.Load(int3(sx,p.y,0)).rgb;
    int bleed = int(round(2*game.z/224));
    a.r = lerp(a.r, picture.Load(int3(max(sx-bleed,0),p.y,0)).r, .16*s);
    a.b = lerp(a.b, picture.Load(int3(min(sx+bleed,int(output_size.x)-1),p.y,0)).b, .16*s);
    uint phase = uint(floor(t*30));
    float n = float((uint(p.x)*1664525u + uint(p.y)*1013904223u + phase*374761393u)&65535u)/65535-.5;
    a += band*(n*25+5)*s/255;
    float3 d = lerp(raw,previous.Load(int3(p,0)).rgb,.19*s);
    d *= 1-.05*max(0,1-fmod(native.y+t*28,144)/7)*s;
    return float4(saturate(lerp(a,d,gb)),1)*color;
}
