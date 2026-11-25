#!/usr/bin/env python3
"""Guard fixed fragment work; rasterized influence quads may grow, scene pixel work may not."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
scene=(ROOT/'assets/shaders/enhanced.frag.hlsl').read_text(encoding='utf-8')
ground=(ROOT/'assets/shaders/mode7.frag.hlsl').read_text(encoding='utf-8')
field=(ROOT/'assets/shaders/influence.frag.hlsl').read_text(encoding='utf-8')
# Extraction and blur are fixed kernels on 56x36; no source-count-dependent scene loops.
ambient_scene = scene[:scene.index('float battle_gaussian(')] + scene[scene.index('float4 main('):]
loops=re.findall(r'\b(?:for|while)\s*\(([^)]*)\)',ambient_scene)
assert len(loops)==2,loops
# Battle lighting has a fixed three-group ceiling and no corner blur, only in the final draw.
battle = scene[scene.index('float3 battle_light('):scene.index('float4 main(')]
assert len(re.findall(r'for \(int [ij]=0; [ij]<3;',battle)) == 2  # enemy lift and stage cones (pools dropped)
assert 'picture.SampleLevel(' not in battle
# The selection outline is UI: preserve its original contour and halo.
outline = scene[scene.index('bool battle_enemy_near('):scene.index('float3 battle_light(')]
assert 'battle_ring(' in outline and '0.55*exp(-(d*d)/0.25)' in outline
assert 'battle.x <= 0' in battle and 'owner.r > 0.5' in battle
assert all(('< 4' in loop or '< 5' in loop) for loop in loops),loops
assert 'lanczos(' not in ground and 'filtered(' not in ground
assert 'sin(' not in field and 'exp(' not in field
for name in ('light_positions[','light_colors[','light_beams[','spell_distance('):assert name not in scene,name
assert 'for (int i = 0; i < int(screen_params.w)' not in ground
assert 'tile_sample(actors[' not in ground
assert 'bits != 0' not in field and 'firstbitlow' not in field
assert 'shadow_field.SampleLevel' in ground
assert 'ambient_field.SampleLevel' in scene and 'spell_field.SampleLevel' in scene
# Count the actual sampling sites in the functions used by scene/restoration fragments.
def function(text,name):
    start=re.search(r'float[234]? '+name+r'\(',text).start();brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}');end+=1
    return text[brace:end]
assert function(scene,'animation_light').count('.SampleLevel(')==1
assert function(scene,'animation_dark').count('.SampleLevel(')==1
assert function(scene,'base').count('shadow_field.SampleLevel(')==1
assert scene.count('ambient_field.SampleLevel(')==2 and scene.count('spell_field.SampleLevel(')==1
assert ground.count('shadow_field.SampleLevel(')==1
assert 'position.w>8.5' not in field and 'floor(uv*grid.xy)' not in field
assert 'SDL_RenderGeometry(renderer_, dummy_, vertices_' not in (ROOT/'src/engine/influence_buffer.cpp').read_text(encoding='utf-8')
print('Fixed influence sample bounds: composite ≤4, restored enemy ≤2, ground shadow ≤1; at most eight soft spell groups; original target ring; no corner blur or occupied-pixel distance loop.')
