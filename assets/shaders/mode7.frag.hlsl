// SDL_GPU tile map and cards. Native camera coordinates rasterize at output resolution.
Texture2D<float4> art : register(t0, space2);
SamplerState art_sampler : register(s0, space2);
Texture2D<float4> words : register(t1, space2);
SamplerState words_sampler : register(s1, space2);
Texture2D<float4> indices : register(t2, space2);
SamplerState indices_sampler : register(s2, space2);
Texture2D<float4> palettes : register(t3, space2);
SamplerState palettes_sampler : register(s3, space2);
Texture2D<float4> water : register(t4, space2);
SamplerState water_sampler : register(s4, space2);
Texture2D<float4> ui : register(t5, space2);
SamplerState ui_sampler : register(s5, space2);
Texture2D<float4> mip0 : register(t6, space2);
SamplerState mip0_sampler : register(s6, space2);
Texture2D<float4> mip1 : register(t7, space2);
SamplerState mip1_sampler : register(s7, space2);
Texture2D<float4> mip2 : register(t8, space2);
SamplerState mip2_sampler : register(s8, space2);
Texture2D<float4> mip3 : register(t9, space2);
SamplerState mip3_sampler : register(s9, space2);
Texture2D<float4> mip4 : register(t10, space2);
SamplerState mip4_sampler : register(s10, space2);
Texture2D<float4> shadow_field : register(t11, space2);
SamplerState shadow_sampler : register(s11, space2);
Texture2D<float4> scanlines : register(t12, space2);
SamplerState scanline_sampler : register(s12, space2);
cbuffer Params : register(b0, space3) {
    float4 control; // pass (9: minimap), card, flat field pixel scale (0=perspective), field snap / unfold amount
    float4 camera; // focal length, sin(tilt), cos(tilt), distance
    float4 scroll; // world scroll x/y, flat field origin relative to scroll
    float4 card; // screen-space foot x/y, width/height
    float4 shape; // bottom, extra stretch, shaft tip, front
    float4 rectangle; // draw bounds in native screen coordinates
    float4 screen_params; // output pixels/native x/y, IPS strength, ground shadow count
    float4 raster; // x: final output pixels per scene pixel (actor integer size and anchor grid)
    float4 actors[20]; // world-space feet and ground ellipse radii
};
float row_value(float x, float y) {
    uint4 bytes = uint4(round(scanlines.SampleLevel(scanline_sampler,float2(x,y),0)*255));
    return asfloat((bytes.a<<24)|(bytes.r<<16)|(bytes.g<<8)|bytes.b);
}
float2 ground(float2 screen) {
    if (control.z > 0) return scroll.zw + screen * screen_params.xy / control.z;
    if (control.y < 0.5) {
        float dy = row_value(0.25,screen.y/144);
        float slope = row_value(0.75,screen.y/144);
        return float2(112+(screen.x-112)*slope,79+dy);
    }
    float v = screen.y - 79;
    float dy = v * camera.x * camera.w / (camera.x * camera.z + v * camera.y);
    return float2(112 + (screen.x - 112) * (camera.x * camera.w - dy * camera.y) / camera.x, 79 + dy);
}
// Shared integer lookup for color, foreground ownership, and reviewed water.
// Beyond the authored map, repeat its outermost two border cells outward (World::extend in mode7.cpp):
// two cells keep the borders' 2x2 patterns. The loaded grid's top two rows (y 0..31) are cell 00 filler.
float2 extend_edges(float2 p) {
    float2 first = float2(0, 32);
    float2 low = first + (p - first) - 32 * floor((p - first) / 32);
    float2 high = 1248 + (p - 1248) - 32 * floor((p - 1248) / 32);
    return float2(p.x < first.x ? low.x : p.x >= 1280 ? high.x : p.x,
                  p.y < first.y ? low.y : p.y >= 1280 ? high.y : p.y);
}
float4 tile_sample(float2 position, int pass) {
    int2 p = int2(clamp(floor(position), 0, 1279));
    float3 data = floor(words.SampleLevel(words_sampler, (p / 8 + 0.5) / 160, 0).rgb * 255 + 0.5);
    uint word = uint(data.r) | (uint(data.g) << 8);
    uint tile = (word & 511) | ((word & 8192) != 0 ? 512 : 0);
    int2 local = p & 7;
    if (word & 16384) local.x = 7 - local.x;
    if (word & 32768) local.y = 7 - local.y;
    int2 at = int2(tile % 32, tile / 32) * 8 + local;
    if (pass == 1) return float4(0, data.b / 255, 0, 1);
    if (pass == 2) {
        at.y += int(word >> 14) * 256;
        float kind = water.SampleLevel(water_sampler, (at + 0.5) / float2(256,1024), 0).r;
        return float4(kind > 0 ? float2((p - 128) & 255) / 255 : 0, kind, 1);
    }
    uint address = tile * 8 + local.y; // one RGBA texel holds four packed bytes = one tile row
    uint4 bytes = uint4(round(indices.SampleLevel(indices_sampler,
        (float2(address % 128, address / 128) + 0.5) / float2(128,64), 0) * 255));
    uint byte = local.x < 2 ? bytes.b : local.x < 4 ? bytes.g : local.x < 6 ? bytes.r : bytes.a;
    uint index = (local.x & 1) != 0 ? byte & 15 : byte >> 4;
    return palettes.SampleLevel(palettes_sampler, (float2(index, (word >> 9) & 15) + 0.5) / 16, 0);
}
float3 mip_sample(float2 uv, int level) {
    if (level == 0) return mip0.SampleLevel(mip0_sampler, uv, 0).rgb;
    if (level == 1) return mip1.SampleLevel(mip1_sampler, uv, 0).rgb;
    if (level == 2) return mip2.SampleLevel(mip2_sampler, uv, 0).rgb;
    if (level == 3) return mip3.SampleLevel(mip3_sampler, uv, 0).rgb;
    return mip4.SampleLevel(mip4_sampler, uv, 0).rgb;
}
// A one-output-pixel boundary strip, integrated across the output pixel. Derivatives
// follow the ground/card's WSC texels; fading small cells suppresses moire during motion.
float grid_integral(float p, float width) {
    return floor(p) * width + max(0, frac(p) - (1 - width));
}
float grid_axis(float p, float footprint) {
    float cell = 1 / max(footprint, 0.00001);
    float fade = smoothstep(1.5, 2.5, cell); // full strength down to ~2-pixel cells
    float width = min(1, footprint);
    float coverage = (grid_integral(p + footprint * 0.5, width) -
                      grid_integral(p - footprint * 0.5, width)) / max(footprint, 0.00001);
    // The flat IPS grid's look at its usual 6x (2/3 of a 140/255 dark line), whatever the cell size, so it reads
    // as clearly as the flat grid near the party.
    return 1 - saturate(coverage) * (2.0 / 3) * (115.0/255) * fade * screen_params.z;
}
float grid(float2 source) {
    float2 footprint = max(abs(ddx(source)), abs(ddy(source)));
    return screen_params.z > 0 ? grid_axis(source.x, footprint.x) * grid_axis(source.y, footprint.y) : 1;
}
// Integer flags survive RGBA8 without filtering: R UI, G foreground/card, B UI margin or actor.
float packed_owner(float3 owner) {
    uint flags = (owner.r > 0.5 ? 1 : 0) | (owner.g > 0.1 ? 2 : 0) |
                 (owner.b > 0.5 ? 4 : 0) | (owner.b > 0 && owner.b < 0.01 ? 8 : 0);
    return float(flags) / 255;
}
// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    if (control.x > 8.5) {
        // World-map minimap: the 80x80 mip, one nearest texel per tile; brightness, saturation, opacity.
        float3 rgb = art.SampleLevel(art_sampler, uv, 0).rgb;
        float luma = dot(rgb, float3(0.299, 0.587, 0.114));
        return float4(lerp(float3(luma, luma, luma), rgb, control.z) * control.y, control.w);
    }
    if (control.x > 6.5) {
        // Two RGBA8 texels carry exact float32 bits per scanline. No half-float loss near WSC
        // texel boundaries. Compute the same operations once per row rather than once per output pixel.
        float v = uv.y*144-79;
        float dy = v*camera.x*camera.w/(camera.x*camera.z+v*camera.y);
        float slope = (camera.x*camera.w-dy*camera.y)/camera.x;
        uint bits = asuint(uv.x < 0.5 ? dy : slope);
        return float4((bits>>16)&255,(bits>>8)&255,bits&255,bits>>24)/255;
    }
    if (control.x > 5.5) {
        // Flat field pixel borders as a multiply pass over the finished picture: effects snapped to the
        // field's pixels would resample lines drawn into it away. Screen-space UI stays clear.
        float2 native = uv * float2(224,144);
        // Ownership B=1 marks projected actor pixels; their own card grid is already present.
        // Sampling the ground lattice over a stationary actor makes its pixels shimmer while walking.
        uint flags = uint(round(art.SampleLevel(art_sampler, uv, 0).a * 255));
        float actor = (flags & 8) != 0 ? 1.0/255 : 0;
        float lines = ui.SampleLevel(ui_sampler, uv, 0).r > 0.5 || (actor > 0 && actor < 0.01)
                          ? 1 : grid(ground(native) + scroll.xy + 128);
        return float4(lines, lines, lines, 1);
    }
    if (control.x > 4.5) {
        // The ordinary IPS mask is composited only on restored screen-space UI.
        float2 low = (floor(uv * float2(224,144) - 0.0001) + 0.5) / float2(224,144);
        float2 high = (floor(uv * float2(224,144) + 0.0001) + 0.5) / float2(224,144);
        float coverage = max(max(ui.SampleLevel(ui_sampler, low, 0).b, ui.SampleLevel(ui_sampler, high, 0).b),
                             max(ui.SampleLevel(ui_sampler, float2(low.x,high.y), 0).b,
                                 ui.SampleLevel(ui_sampler, float2(high.x,low.y), 0).b));
        float4 axes = art.SampleLevel(art_sampler, uv, 0);
        float3 across = 1 - screen_params.z * (1 - axes.rgb);
        float down = 1 - screen_params.z * (1 - axes.a);
        return coverage > 0.5 ? float4(across * down, 1) : float4(1,1,1,1);
    }
    if (control.x > 3.5) {
        // 2x2 area reduction: mip texels integrate source WSC blocks exactly.
        float d = 0.5 / scroll.z;
        return (art.SampleLevel(art_sampler, uv + float2(-d,-d),0) +
                art.SampleLevel(art_sampler, uv + float2(d,-d),0) +
                art.SampleLevel(art_sampler, uv + float2(-d,d),0) +
                art.SampleLevel(art_sampler, uv + float2(d,d),0)) * 0.25;
    }
    if (control.x > 2.5) {
        // Cache exact foreground ownership beside resolved color. Only mip 0's alpha is used;
        // the reduced levels' averaged alpha never participates in ownership or water.
        // The filler rows hold the border's repeat too, so filtered mips never blend filler at the fold.
        float2 cached = extend_edges(uv*1280);
        return float4(tile_sample(cached,0).rgb,tile_sample(cached,1).g);
    }
    float2 screen = rectangle.xy + uv * rectangle.zw;
    float2 screen_uv = screen / float2(224,144);
    float3 protected_ui = ui.SampleLevel(ui_sampler, screen_uv, 0).rgb;
    float2 unfolded = ground(screen) + scroll.xy + 128; // continuous, for derivatives and the grid
    float2 world = extend_edges(unfolded);
    if (control.y < 0.5 && control.x > 1.5)
        return float4(protected_ui.r > 0.5 ? 0 : tile_sample(world, 2).rgb, 1);
    float4 native_texel = mip0.SampleLevel(mip0_sampler,(floor(clamp(world,0,1279))+0.5)/1280,0);
    float4 floor_owner = float4(0,native_texel.a,0,1);
    if (control.y < 0.5) {
        if (control.x > 1.5) return float4(protected_ui.r > 0.5 ? 0 : tile_sample(world, 2).rgb, 1);
        if (control.x > 0.5) return float4(protected_ui.r, floor_owner.g, protected_ui.b, 1);
        // The cached level-zero atlas is the same integer tile/palette resolve. Avoid dependent
        // index and palette fetches, including when minification will replace the nearest color.
        float3 pixel;
        // Derivatives are in output pixels: magnification remains exact nearest WSC blocks.
        float footprint = max(length(ddx(unfolded)), length(ddy(unfolded)));
        float lod = clamp(log2(max(1,footprint)),0,4);
        if (footprint > 1) {
            // Keep native WSC blocks at magnification. Blend adjacent cached mips for minification;
            // the pyramid integrates native cells and bilinear filtering makes moving samples continuous.
            int level = int(floor(lod));
            float3 smooth = mip_sample(world / 1280, level);
            if(level<4 && frac(lod)>0) smooth=lerp(smooth,mip_sample(world/1280,level+1),frac(lod));
            pixel = lerp(native_texel.rgb,smooth,saturate((footprint-1)*2));
        } else pixel = native_texel.rgb;
        float shadow = screen_params.w > 0 && protected_ui.r < 0.5 && floor_owner.g < 0.1
            ? shadow_field.SampleLevel(shadow_sampler,screen_uv,0).r * screen_params.w : 0;
        return float4(pixel * (1 - shadow) * (protected_ui.r > 0.5 ? 1 : grid(unfolded)),
                      packed_owner(float3(protected_ui.r, floor_owner.g, protected_ui.b))) * color;
    }
    if (protected_ui.r > 0.5 || (shape.w < 0.5 && floor_owner.g > 0.1)) discard;
    float f = camera.x, sine = camera.y, cosine = camera.z;
    float a = control.y > 1.5 ? control.w : 1; // landmarks unfold; actors stay upright
    float base = card.y - 79;
    float scale_at_foot = f / (f * camera.w - base * sine);
    float2 foot = float2(112 + (card.x - 112) * scale_at_foot, 79 + base * cosine * scale_at_foot);
    if (control.z > 0) {
        scale_at_foot = control.z / screen_params.x;
        foot = (card.xy - scroll.zw) * control.z / screen_params.xy;
    }
    float2 shift = 0;
    float v = screen.y - 79 - shift.y;
    float d = (base * f * cosine - v * (f * camera.w - base * sine)) /
              (f * ((1 - a) * cosine + a) + v * (1 - a) * sine);
    float scale = f / (f * camera.w - (base - d * (1 - a)) * sine);
    float cx = (screen.x - 112 - shift.x) / scale + 112 - card.x + card.z / 2;
    float source_d = d;
    if (shape.y > 0) {
        float below_shaft = card.w - shape.y - shape.z;
        if (d >= below_shaft + shape.y * a) source_d += shape.y * (1 - a);
        else if (d > below_shaft && a > 0) source_d = below_shaft + (d - below_shaft) / a;
    }
    float cy = shape.x - source_d;
    if (control.y < 1.5) {
        // Output-space integer scale for actors. Round the foot to the output lattice;
        // use the same anchor for all rows/columns so every source pixel is k by k.
        float reference_scale = max(1,raster.x);
        float2 output_grid = screen_params.xy * reference_scale;
        float k = max(1, floor(scale_at_foot * output_grid.x + 0.5)) / reference_scale;
        float2 anchor = floor(foot * output_grid + 0.5) / reference_scale;
        if (control.z > 0 && frac(control.z * reference_scale) > 0) {
            // Inside a flat field's fidelity fade the scale is fractional: follow the ground exactly.
            k = scale_at_foot * screen_params.x;
            anchor = foot * screen_params.xy;
        }
        float2 local = (screen * screen_params.xy - anchor) / k;
        cx = local.x + card.z / 2;
        cy = shape.x + local.y;
    }
    if (scale <= 0 || cx < 0 || cx >= card.z || cy < 0 || cy >= card.w) discard;
    uint width, height; art.GetDimensions(width,height);
    float2 size = float2(width,height);
    float2 source = floor(floor(float2(cx,cy)) * size / card.zw);
    float4 pixel = art.SampleLevel(art_sampler, (source + 0.5) / size, 0);
    // Half-alpha texels (see-through terrain) draw only in the actor's veil pass (control.y 1.25), blended.
    bool veil_pass = control.y > 1.1 && control.y < 1.4, half = pixel.a > 0.25 && pixel.a < 0.75;
    if (pixel.a < 0.25 || half != veil_pass) discard;
    if (veil_pass) return float4(pixel.rgb * grid(float2(cx,cy) * size / card.zw), 0.5) * color;
    if (control.x > 1.5) return float4(0,0,0,1);
    if (control.x > 0.5) return float4(protected_ui.r, 64.0/255, max(protected_ui.b, control.y < 1.5 ? 1.0/255 : 0), 1);
    return float4(pixel.rgb * grid(float2(cx,cy) * size / card.zw),
                  packed_owner(float3(protected_ui.r, 64.0/255, max(protected_ui.b, control.y < 1.5 ? 1.0/255 : 0)))) * color;
}
