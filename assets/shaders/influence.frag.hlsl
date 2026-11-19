// Each draw shades only one bounded light/shadow quad. Max blending preserves the overlap.
Texture2D<float4> picture : register(t0, space2);
SamplerState picture_sampler : register(s0, space2);
cbuffer Params : register(b0, space3) {
    float4 position, emission, extent;
    float4 options; // plane: field / spell LCD / spell Raw+gloom / shadow; beam style, phase, snap
    float4 grid; // effect cells; world shadow: output pixels/native x/y, flat pixel scale, unused
    float4 camera; // focal, sin tilt, cos tilt, distance
    float4 scroll; // world scroll x/y, flat origin x/y
};
// All spatial falloff is cached in stamps; the GPU only samples and max-combines.
// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    float amount;
    if(extent.z>3.5) {
        if(extent.w>0.5) {
            float phase=frac(options.z/960)*16;
            float a=floor(phase),b=fmod(a+1,16);
            // Half-texel inset stops adjacent atlas phases leaking at quad borders.
            uint w,h;picture.GetDimensions(w,h);
            float y=clamp(uv.y,0.5/w,1-0.5/w);
            amount=lerp(picture.SampleLevel(picture_sampler,float2(uv.x,(y+a)/16),0).r,
                        picture.SampleLevel(picture_sampler,float2(uv.x,(y+b)/16),0).r,frac(phase));
        } else amount=picture.SampleLevel(picture_sampler,uv,0).r;
    } else if(extent.z>2.5) {
        float2 q=options.w>0.5 ? (floor(uv*scroll.zw)+0.5)/scroll.zw : uv;
        amount=picture.SampleLevel(picture_sampler,q,0).r;
    } else {
        float2 q=options.w>0.5 ? (floor(uv*grid.xy)+0.5)/grid.xy : uv;
        amount=picture.SampleLevel(picture_sampler,(q*float2(224,144)-scroll.xy)/scroll.zw,0).r;
        return position.w>8.5 ? float4(0,0,0,amount*emission.a)
            : float4(emission.rgb*(amount*emission.a),0);
    }
    return color.a<0 ? float4(0,0,0,amount*-color.a) : float4(color.rgb*(amount*color.a),0);
}
