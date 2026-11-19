// SDL renderer ABI. All sampling uses clamped textures; no simulation clock or RNG.
Texture2D<float4> picture : register(t0, space2);
SamplerState picture_sampler : register(s0, space2);
Texture2D<float4> ownership : register(t1, space2);
SamplerState ownership_sampler : register(s1, space2);
Texture2D<float4> glow : register(t2, space2);
SamplerState glow_sampler : register(s2, space2);
Texture2D<float4> gb_picture : register(t3, space2);
SamplerState gb_sampler : register(s3, space2);
Texture2D<float4> original_picture : register(t4, space2);
SamplerState original_sampler : register(s4, space2);
Texture2D<float4> water_map : register(t5, space2);
SamplerState water_sampler : register(s5, space2);
Texture2D<float4> ambient_field : register(t6, space2);
SamplerState influence_sampler : register(s6, space2);
Texture2D<float4> spell_field : register(t7, space2);
SamplerState spell_sampler : register(s7, space2);
Texture2D<float4> spell_raw : register(t8, space2);
SamplerState raw_sampler : register(s8, space2);
Texture2D<float4> shadow_field : register(t9, space2);
SamplerState shadow_sampler : register(s9, space2);
Texture2D<float4> water_pattern : register(t10, space2);
SamplerState pattern_sampler : register(s10, space2);
Texture2D<float4> multiply_layer : register(t11, space2);
SamplerState multiply_sampler : register(s11, space2);
cbuffer Params : register(b0, space3) {
    float4 control; // mode (extract / horizontal / composite), snap, grade, bloom
    float4 scene;   // shadows, GB weight, actor count, fade exposure
    float4 actors[20]; // feet and ellipse radii, native coordinates
    float4 lighting; // light count, slider strength, reserved
    float4 water; // strength, bounded frame phase, effect weight, gloom present
    float4 metadata; // packed scene ownership, field light present, additive spell present
    float4 dark_color; // shared subtractive animation target (native effect table)
    float4 effect_grid; // projected WSC cells across the scene; a zoomed flat field's origin in the game's view
    float4 battle; // lighting weight (Enhanced fade and option), per-group target selection (y, z, w)
    float4 battle_groups[3]; // center x/y, picture bottom, gain; 0 gain = absent
    float4 palette_fade; // native lower/upper channel limits
};
// Apply the native palette's remaining color range to every authored effect at its final composition.
// A collapsed range restores the native bytes exactly, including LCD-matrix roundoff at white.
float3 faded_effect(float3 native, float3 enhanced) {
    float span = palette_fade.y-palette_fade.x;
    return lerp(native,clamp(enhanced,palette_fade.x,palette_fade.y),span);
}
float4 owner_at(float2 uv, float lod) {
    float4 value = ownership.SampleLevel(ownership_sampler, uv, lod);
    if (metadata.x < 0.5) return value;
    uint flags = uint(round(value.a * 255));
    return float4((flags & 1) != 0 ? 1 : 0, (flags & 2) != 0 ? 64.0/255 : 0,
                  (flags & 4) != 0 ? 1 : ((flags & 8) != 0 ? 1.0/255 : 0), 1);
}
float2 effect_uv(float2 uv) {
    return control.y > 0.5 ? (floor(uv * effect_grid.xy) + 0.5) / effect_grid.xy : uv;
}
// The look was tuned through the LCD matrix. Apply that look in
// the same comparison space, then invert the matrix to retain Raw presentation.
float3 to_lcd(float3 c) {
    return float3(dot(c,float3(13,2,1)), dot(c,float3(0,12,4)), dot(c,float3(3,2,11))) / 17;
}
float3 to_raw(float3 c) {
    return float3(dot(c,float3(1.3175,-0.2125,-0.0425)),
                  dot(c,float3(0.1275,1.4875,-0.5525)),
                  dot(c,float3(-0.3825,-0.2125,1.6575)));
}
float3 graded(float3 c) {
    float3 warm = c * float3(1.15, 1.045, 0.85);
    warm += 0.045 * scene.w * (1 - warm) * float3(1,0.6,0);
    warm = saturate(warm);
    warm = saturate(warm + 0.18 * (warm - 0.5) * (1 - abs(2 * warm - 1)));
    return lerp(c, warm, control.z);
}
float3 base(float2 uv) {
    float3 c = picture.SampleLevel(picture_sampler, uv, 0).rgb;
    float2 mask = owner_at( uv, 0).rg;
    if (mask.r > 0.5) return to_lcd(c);
    float shadow = 0;
    if (scene.x > 0.5 && scene.z > 0 && (mask.g < 0.1 || mask.g > 0.5)) {
        float2 position = effect_uv(uv) * float2(224,144);
        shadow = shadow_field.SampleLevel(shadow_sampler, effect_uv(uv), 0).r;
    }
    return graded(to_lcd(c * (1 - shadow)));
}
// One metadata sample on scene pixels; two extra samples for a faint edge reflection.
// Blue layer/shimmer is after bloom, so tiny sparkle pixels never become large glows.
float3 watery(float3 c, float2 uv) {
    if (water.x <= 0) return c;
    float3 data = floor(water_map.SampleLevel(water_sampler, uv, 0).rgb * 255 + 0.5);
    if (data.b < 1) return c;
    float kind = floor(data.b / 16), depth = data.b - kind * 16;
    float2 p = data.rg;
    float time = kind == 3 ? water.y * 0.5 : water.y;
    float phase = frac(time/960)*32;
    float2 at=(frac(p/256)*64+1.5)/66;
    float2 pattern=lerp(water_pattern.SampleLevel(pattern_sampler,float2(at.x,(at.y+floor(phase))/32),0).rg,
                       water_pattern.SampleLevel(pattern_sampler,float2(at.x,(at.y+fmod(floor(phase)+1,32))/32),0).rg,frac(phase));
    float exposure = metadata.x>0.5 ? scene.w : min(scene.w, saturate(max(c.r,max(c.g,c.b)) * 2));
    float amount = min(water.x, 3) * exposure;
    float deep = saturate(depth/12);
    float3 blue = lerp(float3(0.10,0.48,0.76), float3(0.055,0.24,0.50), deep);
    // 24% translucent tint at 100%; original tile art stays visible (16% was too faint in-game).
    c = lerp(c, blue * exposure, saturate(0.24 * amount));
    if (depth <= 8) {
        float shift = (pattern.g*2-1)*1.2;
        if (control.y > 0.5) shift = round(shift);
        float2 source = uv + float2(shift, 1 - 2*depth) / float2(224,144);
        // Never reflect UI, including translucent badges; don't clamp an offscreen shoreline.
        if (source.y >= 0 && source.x >= 0 && source.x < 1 &&
            owner_at( source, 0).r < 0.5) {
            float3 reflection = to_lcd(original_picture.SampleLevel(original_sampler, source, 0).rgb);
            c = lerp(c, reflection * float3(0.65,0.82,1), saturate(amount*0.12*(1-depth/9)));
        }
    }
    float motion = kind == 3 ? 0.5 : 1;
    return saturate(c + float3(0.19,0.32,0.38) * (amount*motion*(pattern.r*0.32)));
}
// Influence fields are precombined with max blending. These paths have fixed sample counts.
float3 animation_light(float2 q) {
    return saturate(spell_raw.SampleLevel(raw_sampler, q, 0).rgb * lighting.y);
}
float3 animation_dark(float2 q, float3 c) {
    if (water.w < 0.5) return c;
    float amount = saturate(spell_raw.SampleLevel(raw_sampler, q, 0).a * lighting.y);
    return c - max(c - dark_color.rgb, 0) * amount;
}
// Compose the small field effects as an affine layer: c' = c * multiply + add.
// Art never enters this layer, so a tiny buffer cannot spread an actor's palette onto the floor.
float4 field_layer(float2 q,bool multiply) {
    float3 bloom=control.w>0 && scene.w>0 ? saturate(glow.SampleLevel(glow_sampler,q,0).rgb*(1.35*control.w)) : 0;
    float3 light=metadata.y>0.5 && lighting.y>0 ? saturate(ambient_field.SampleLevel(influence_sampler,q,0).rgb*(lighting.y*scene.w)) : 0;
    float3 additive=1-(1-bloom)*(1-light);
    float3 upper=1;
    if(owner_at(q,0).r<0.5) { additive=watery(additive,q);upper=watery(upper,q); }
    return float4(multiply ? max(upper-additive,0) : additive,1);
}
float battle_gaussian(float2 delta, float2 radius) {
    float2 d = delta / radius;
    return exp(-dot(d,d));
}
// The enemy group a native cell belongs to: an enemy pixel, nearest group center (pictures never overlap).
int battle_enemy_group(float2 cell) {
    float2 owner = owner_at((cell+0.5)/float2(224,144),0).rg;
    if (owner.r > 0.5 || owner.g <= 0.998) return -1;
    int nearest = -1;
    float best = 1e9;
    [unroll] for (int i=0; i<3; ++i)
        if (battle_groups[i].w > 0 && abs(cell.x+0.5-battle_groups[i].x) < best) {
            best = abs(cell.x+0.5-battle_groups[i].x);
            nearest = i;
        }
    return nearest;
}
bool battle_enemy_near(float2 cell, float2 step, int group) {
    return battle_enemy_group(cell+step) == group || battle_enemy_group(cell+2*step) == group;
}
// A background cell touching (8-neighborhood) one of group `group`'s pixels, outside the picture: a gap
// with the enemy within two cells on both sides (between legs, under an arm) is not outline.
bool battle_ring(float2 cell, int group) {
    if (battle_enemy_group(cell) >= 0) return false;
    if ((battle_enemy_near(cell,float2(-1,0),group) && battle_enemy_near(cell,float2(1,0),group)) ||
        (battle_enemy_near(cell,float2(0,-1),group) && battle_enemy_near(cell,float2(0,1),group)))
        return false;
    [unroll] for (int y=-1; y<=1; ++y)
        [unroll] for (int x=-1; x<=1; ++x)
            if ((x != 0 || y != 0) && battle_enemy_group(cell+float2(x,y)) == group) return true;
    return false;
}
// Target outline: a 1 native px warm near-white ring just outside the targeted enemy's
// own outline, with a 0.5 px glow; it follows the selection's crossfade and pulses gently with the target light.
float battle_outline(float2 p) {
    float result = 0;
    float2 cell = floor(p);
    [unroll] for (int i=0; i<3; ++i) {
        float selected = battle[i+1];
        float4 g = battle_groups[i];
        if (selected <= 0 || g.w <= 0 || any(abs(p-g.xy) > float2(48,56))) continue;
        float coverage = 0;
        [unroll] for (int y=-1; y<=1; ++y)
            [unroll] for (int x=-1; x<=1; ++x) {
                float2 at = cell+float2(x,y);
                if (!battle_ring(at,i)) continue;
                float d = length(max(abs(p-(at+0.5))-0.5,0)); // native px from that ring cell
                coverage = max(coverage, d <= 0 ? 1 : 0.55*exp(-(d*d)/0.25));
            }
        float pulse = saturate((g.w-1)/(1.2*selected));
        result = max(result, coverage*selected*(0.7+0.3*pulse));
    }
    return result;
}
// Runs in the final scene draw. Enemy art never enters the blurred source.
float3 battle_light(float3 c, float2 uv, float2 owner) {
    float2 p = uv * float2(224,144);
    // The battlefield is rows 0-79: the native header rows hold the backdrop (the HP plates are UI, kept above).
    if (battle.x <= 0 || p.y >= 80 || owner.r > 0.5) return c;
    bool enemy = owner.g > 0.998;
    // Protect effect art, occluded enemies, ice and other screen-2/sprite overlays exactly.
    if (!enemy && owner.g > 0.1) return c;
    float3 lit = c;
    if (enemy) {
        [unroll] for (int i=0; i<3; ++i) {
            float4 g = battle_groups[i];
            if (g.w <= 0) continue;
            float k = 0.15 * (g.w-1) * saturate(1.6*battle_gaussian(p-g.xy,float2(40,48)));
            lit = lit*(1+k) + k*0.25*scene.w*float3(0.12,0.06,0);
        }
    } else {
        float r = length((p-float2(112,40))/float2(112,40));
        float vignette = smoothstep(0,1,saturate((r-1)/0.45));
        if (vignette > 0) {
            float3 blurred = 0;
            // Nine bilinear taps, sigma about one native pixel. Clamp to battlefield rows,
            // so the panels below cannot leak into the corners.
            [unroll] for (int y=-1; y<=1; ++y)
                [unroll] for (int x=-1; x<=1; ++x) {
                    float2 at = clamp(p+float2(x,y)*1.1824,float2(0.5,0.5),float2(223.5,79.5));
                    float wx = x == 0 ? 0.40262 : 0.29869;
                    float wy = y == 0 ? 0.40262 : 0.29869;
                    blurred += picture.SampleLevel(picture_sampler,at/float2(224,144),0).rgb*wx*wy;
                }
            lit = lerp(lit,blurred,vignette);
        }
        lit *= 1-0.12*vignette;
        float cone = 0;
        [unroll] for (int j=0; j<3; ++j)
            if (battle_groups[j].w > 0)
                cone = max(cone,battle_gaussian(p-battle_groups[j].xy,float2(34,40)));
        lit *= 1-0.18*(1-cone);
        if (any(battle.yzw > 0)) lit = lerp(lit,float3(1,0.91,0.784),battle_outline(p));
    }
    return lerp(c,faded_effect(c,saturate(lit)),battle.x);
}
// TEXCOORD0/1 are the names SDL's renderer vertex shader gives color and uv; Direct3D 12 links stages by name.
float4 main(float4 color : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0 {
    // Only host UI runs at output resolution after a capped scene. Copy its ordinary scaled colors
    // directly; scene ownership, lighting, and effect-grid calculations are not needed here.
    if (control.x > 5.1 && control.x < 5.5) {
        if (ownership.SampleLevel(ownership_sampler,uv,0).r <= 0.5) discard;
        return float4(original_picture.Sample(original_sampler,uv).rgb,1)*color;
    }
    float2 q = effect_uv(uv);
    if(control.x>5.5 && control.x<6.5) {
        float3 sharp=picture.SampleLevel(picture_sampler,uv,0).rgb;
        float3 c=graded(to_lcd(sharp))*multiply_layer.SampleLevel(multiply_sampler,uv,0).rgb+
            glow.SampleLevel(glow_sampler,uv,0).rgb;
        c=lerp(sharp,faded_effect(sharp,saturate(to_raw(c))),water.z);
        float2 gb_uv=(uv*effect_grid.xy+effect_grid.zw)/float2(224,144);
        if(all(gb_uv>=0) && all(gb_uv<1)) c=lerp(c,gb_picture.SampleLevel(gb_sampler,gb_uv,0).rgb,scene.y);
        return float4(c,1)*color;
    }
    if(metadata.x>0.5 && (control.x>6.5 || (control.x>1.5 && control.x<2.5)))
        return field_layer(q,control.x>6.5)*color;
    if ((control.x > 2.5 && control.x < 3.5) || control.x > 4.5) {
        // Restore ordinarily scaled UI and enemies with exact coverage (no margin): the CPU
        // mask is restoration_coverage(), owned pixels only, in every Enhanced path. A margin
        // would show the ordinary (unzoomed) picture around windows over the Enhanced scene.
        // One lookup covers translucent chrome; enemy pixels keep their exact native coverage.
        float coverage = owner_at(uv,0).b;
        float3 restored;
        float2 owner = owner_at( q, 0).rg;
        // Solid ice owns its interior even next to ordinarily protected UI/enemies. Their filter
        // coverage must never restore wallpaper or portrait pixels over the palette-locked fill.
        if (owner.r < 0.5 && owner.g > 0.49 && owner.g < 0.51) coverage = 0;
        if (coverage <= 0.5) {
            if (control.x < 4.5) discard;
            restored = picture.Sample(picture_sampler, uv).rgb;
        } else {
            restored = control.x > 4.5 ? original_picture.Sample(original_sampler, uv).rgb
                                       : picture.Sample(picture_sampler, uv).rgb;
        }
        if (coverage > 0.5 && lighting.y > 0 && owner.r < 0.5 && owner.g > 0.998) {
            restored = 1 - (1 - restored) * (1 - animation_light(q));
            restored = lerp(restored, animation_dark(q, restored), water.z * (1 - scene.y));
        }
        if (control.x > 4.5 && (coverage <= 0.5 || owner.g > 0.998))
            restored = battle_light(restored,uv,owner_at(uv,0).rg);
        return float4(restored, 1) * color;
    }
    if (control.x < 0.5) {
        float2 owner = owner_at( q, 0).rg;
        if (owner.r > 0.5 || (owner.g > 0.49 && owner.g < 0.51)) return float4(0,0,0,1);
        float3 c = base(q);
        float bright = dot(c, float3(0.3,0.59,0.11));
        // Only small highlights bloom. A broad white/snow surface has no local
        // brightness excess, even when it passes the absolute threshold.
        // Four wide neighbors reject broad bright surfaces; only the tiny extraction target runs this.
        float surrounding = 0;
        static const float2 neighbors[4] = {float2(-8,-8),float2(8,-8),float2(-8,8),float2(8,8)};
        [unroll] for (int i = 0; i < 4; ++i) {
            float2 at = q + neighbors[i] / float2(224,144);
            float3 sample_color = graded(to_lcd(picture.SampleLevel(picture_sampler, at, 0).rgb));
            surrounding += owner_at(at,0).r > 0.5 ? bright : dot(sample_color,float3(0.3,0.59,0.11));
        }
        float isolated = smoothstep(0.08, 0.22, bright - surrounding/4);
        return float4(c * saturate((bright - 0.595) / (1 - 0.595)) * isolated, 1);
    }
    float3 bloom = 0;
    if (control.x < 1.5 || control.x > 3.5) {
        // Five bilinear taps at half-native resolution: the same ~7 native-pixel glow reach.
        static const float2 taps[5] = {float2(0,0.227027),float2(-1.384615,0.316216),
            float2(1.384615,0.316216),float2(-3.230769,0.070270),float2(3.230769,0.070270)};
        [unroll] for (int i = 0; i < 5; ++i) {
            float2 offset = control.x < 1.5 ? float2(taps[i].x/112.0,0) : float2(0,taps[i].x/72.0);
            bloom += glow.SampleLevel(glow_sampler, uv + offset, 0).rgb * taps[i].y;
        }
        return float4(bloom,1);
    }
    if(control.w>0 && scene.w>0) bloom = glow.SampleLevel(glow_sampler, q, 0).rgb;
    float3 original = original_picture.SampleLevel(original_sampler, uv, 0).rgb;
    float3 c = base(q);
    c = 1 - (1-c) * (1-saturate(bloom * (1.35 * control.w)));
    // Point ambience is composited after bloom, so sources never feed its extraction
    // or blow out through a second glow pass. Screen blending preserves texture detail.
    float3 illumination = 0;
    if (lighting.y > 0 && lighting.x > 0) {
        float2 position = q * float2(224,144);
        // Sprite pixels (G 0x40): a battle animation never lights its own art.
        float owner_g = owner_at( q, 0).g;
        bool sprite_pixel = owner_g > 0.2 && owner_g < 0.3;
        if (metadata.y > 0.5) illumination = ambient_field.SampleLevel(influence_sampler, q, 0).rgb;
        if (!sprite_pixel && metadata.z > 0.5) illumination = max(illumination, spell_field.SampleLevel(spell_sampler, q, 0).rgb);
        illumination = saturate(illumination * (lighting.y * scene.w));
        c = 1 - (1-c) * (1-illumination);
    }
    if (owner_at( q, 0).r < 0.5) c = watery(c, q);
    // Restore UI from the original bytes, including transparent windows. The GB
    // branch never enters an effect and blends only after WSC UI restoration.
    c = saturate(to_raw(c));
    float2 mask = owner_at( uv, 0).rg;
    if (lighting.y > 0 && mask.r < 0.5 && mask.g < 0.1) c = animation_dark(q, c);
    // Ice interior gaps carry an exact native-palette color in the Enhanced source.
    if (mask.g > 0.49 && mask.g < 0.51 && mask.r < 0.5)
        c = picture.SampleLevel(picture_sampler, q, 0).rgb;
    else if (mask.r > 0.5 || mask.g > 0.5) c = original;
    c = lerp(original, faded_effect(original,c), water.z); // Enhanced fades over the ordinary WSC picture.
    // The GB picture only covers the game's own view: inside a zoomed flat field it sits where that view
    // is, registered with the map, and the margins keep the WSC art.
    float2 gb_uv = (uv * effect_grid.xy + effect_grid.zw) / float2(224,144);
    if (all(gb_uv >= 0) && all(gb_uv < 1))
        c = lerp(c, gb_picture.SampleLevel(gb_sampler, gb_uv, 0).rgb, scene.y);
    return float4(c,1) * color;
}
