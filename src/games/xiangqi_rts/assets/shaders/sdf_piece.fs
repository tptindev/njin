#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform vec2 u_resolution; // Dimensions of the quad in pixels
uniform float u_radius;    // Piece radius in pixels
uniform int u_side;        // 0: Red (Sở Quân), 1: Black (Hán Quân)
uniform int u_type;        // 0: General, 1: Advisor, 2: Elephant, 3: Chariot, 4: Cannon, 5: Horse, 6: Pawn
uniform float u_selected;  // 1.0 if selected, 0.0 otherwise
uniform float u_crossed;   // 1.0 if river crossed, 0.0 otherwise
uniform float u_time;      // Elapsed animation time
uniform vec2 u_facing;     // Facing direction vector (normalized)

float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sd_rhombus(vec2 p, vec2 b) {
  vec2 q = abs(p);
  float h = clamp((-2.0 * dot(q, b) + dot(b, b)) / max(dot(b, b), 0.0001), -1.0, 1.0);
  float d = length(q - 0.5 * b * vec2(1.0 - h, 1.0 + h));
  return d * sign(q.x * b.y + q.y * b.x - b.x * b.y);
}

float sd_segment(vec2 p, vec2 a, vec2 b) {
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
  return length(pa - ba * h);
}

float aafill(float d) {
  float aa = max(fwidth(d), 0.001);
  return 1.0 - smoothstep(-aa, aa, d);
}

float aaline(float d, float th) {
  float aa = max(fwidth(d), 0.001);
  return 1.0 - smoothstep(-aa, aa, abs(d) - th * 0.5);
}

void main() {
  // Center coordinates in pixel units, origin at center of the token
  vec2 p = (fragTexCoord - vec2(0.5)) * u_resolution;
  float r = u_radius;

  // 1. Drop shadow beneath the token
  vec2 s_offset = vec2(2.5, 4.5);
  float d_shadow = sd_circle(p - s_offset, r);
  float shadow_alpha = (1.0 - smoothstep(-2.0, 7.5, d_shadow)) * 0.45;
  vec4 col_accum = vec4(0.0, 0.0, 0.0, shadow_alpha);

  // 2. Awakening Golden Lotus Aura (for crossed-river pawns)
  if (u_crossed > 0.5) {
    float angle = atan(p.y, p.x);
    float flare = sin(angle * 7.0 + u_time * 6.0) * 2.0;
    float d_aura = abs(length(p) - (r + 3.2 + flare)) - 1.2;
    float aura_glow = exp(-0.45 * max(d_aura, 0.0)) * 0.75;
    float aura_line = aafill(d_aura);
    vec3 aura_col = vec3(1.0, 0.85, 0.35);
    col_accum.rgb += aura_col * (aura_glow + aura_line * 0.6);
    col_accum.a = max(col_accum.a, clamp(aura_glow + aura_line, 0.0, 1.0));
  }

  // 3. Selection Ring and Crisp Tactical Tick Markers
  if (u_selected > 0.5) {
    float pulse = 0.5 + 0.5 * sin(u_time * 5.0);
    float sel_r = r + 4.0 + pulse * 1.5;
    float d_sel = abs(length(p) - sel_r) - 1.2;
    float sel_line = aafill(d_sel);
    float sel_glow = exp(-0.55 * max(abs(length(p) - sel_r) - 0.8, 0.0)) * 0.45;

    // 4 Crisp rotating tick notches at 90-degree intervals
    float sang = atan(p.y, p.x) + u_time * 0.75;
    float ticks = abs(cos(sang * 2.0));
    float tick_dist = abs(length(p) - (sel_r + 2.0)) - 2.5;
    float tick_line = aafill(tick_dist) * smoothstep(0.97, 0.995, ticks);

    vec3 sel_col = (u_side == 0) ? vec3(1.0, 0.82, 0.25) : vec3(0.92, 0.96, 1.0);
    float sel_total = clamp(sel_line + sel_glow + tick_line * 0.8, 0.0, 1.0);
    col_accum.rgb = mix(col_accum.rgb, sel_col, sel_total);
    col_accum.a = max(col_accum.a, sel_total);
  }

  // 4. Token Outer Disc & 3D Bevel Shading
  float d_token = sd_circle(p, r);
  float token_fill = aafill(d_token);

  if (token_fill > 0.001) {
    float rim_w = max(r * 0.16, 3.4);
    float d_inner = sd_circle(p, r - rim_w);
    float inner_fill = aafill(d_inner);

    // Directional light vector (top-left)
    vec2 ldir = normalize(vec2(-0.55, -0.75));
    vec2 n = normalize(p + vec2(0.0001));

    // Surface diffuse factor along normal
    float diff = clamp(dot(-n, ldir) * 0.45 + 0.55, 0.0, 1.0);
    // Specular highlight along top-left beveled rim
    float spec = pow(max(dot(-n, ldir), 0.0), 9.0) * 0.5;

    // Rim Color
    vec3 rim_col;
    if (u_side == 0) {
      // Red Faction: Lacquered Imperial Vermilion with gold/bronze spec
      vec3 red_dark = vec3(0.50, 0.10, 0.08);
      vec3 red_bright = vec3(0.86, 0.22, 0.18);
      rim_col = mix(red_dark, red_bright, diff) + vec3(spec * 1.1, spec * 0.9, spec * 0.4);
    } else {
      // Black Faction: Polished Obsidian with Jade Cyan edge highlight
      vec3 blk_dark = vec3(0.09, 0.11, 0.13);
      vec3 blk_bright = vec3(0.12, 0.68, 0.62);
      rim_col = mix(blk_dark, blk_bright, diff * 0.75 + 0.25) + vec3(spec * 0.6);
    }

    // Inner Recessed Disc: Antique Sandalwood (Red) / Deep Ebony (Black)
    float inner_shadow = (1.0 - smoothstep(0.0, rim_w * 0.75, -d_inner)) * max(dot(n, -ldir), 0.0) * 0.40;
    vec3 base_col;
    if (u_side == 0) {
      base_col = vec3(0.93, 0.90, 0.83) * (1.0 - inner_shadow);
    } else {
      base_col = vec3(0.15, 0.17, 0.19) * (1.0 - inner_shadow * 0.6);
    }

    // Blend Rim and Base
    vec3 token_col = mix(rim_col, base_col, inner_fill);

    // Fine dark line between rim and base
    float d_rim_line = abs(d_inner) - 0.6;
    token_col = mix(token_col, vec3(0.12, 0.12, 0.12), aafill(d_rim_line) * 0.6);

    // 5. Inner Engraved Concentric Ring
    float ring_r = r - rim_w - 2.6;
    if (ring_r > 5.0) {
      float d_ring = abs(sd_circle(p, ring_r)) - 0.7;
      vec3 ring_col = (u_side == 0) ? vec3(0.78, 0.18, 0.15) : vec3(0.12, 0.70, 0.64);
      token_col = mix(token_col, ring_col, aafill(d_ring) * inner_fill * 0.85);
    }

    // 6. Insignia Relief Motif (carved into lower portion, scaled proportionally)
    float ins_scale = clamp(r / 24.0, 0.65, 1.4);
    vec2 ip = (p - vec2(0.0, r * 0.38)) / ins_scale;
    float d_ins = 999.0;

    if (u_type == 0) {
      // General: Triple Stars
      float s1 = sd_circle(ip, 1.8);
      float s2 = sd_circle(ip - vec2(-5.2, -1.0), 1.4);
      float s3 = sd_circle(ip - vec2(5.2, -1.0), 1.4);
      d_ins = min(s1, min(s2, s3));
    } else if (u_type == 1) {
      // Advisor: Twin Shield Diamonds
      float sh1 = sd_rhombus(ip - vec2(-4.0, 0.0), vec2(2.0, 2.7));
      float sh2 = sd_rhombus(ip - vec2(4.0, 0.0), vec2(2.0, 2.7));
      d_ins = min(sh1, sh2);
    } else if (u_type == 2) {
      // Elephant: Bastion Tusks
      float t1 = sd_segment(ip, vec2(-5.0, 1.5), vec2(-1.5, -1.2)) - 0.75;
      float t2 = sd_segment(ip, vec2(5.0, 1.5), vec2(1.5, -1.2)) - 0.75;
      d_ins = min(t1, t2);
    } else if (u_type == 3) {
      // Chariot: Dual Axle & Wheels
      float w1 = sd_circle(ip - vec2(-4.5, 0.0), 1.5);
      float w2 = sd_circle(ip - vec2(4.5, 0.0), 1.5);
      float ax = sd_segment(ip, vec2(-4.5, 0.0), vec2(4.5, 0.0)) - 0.8;
      d_ins = min(min(w1, w2), ax);
    } else if (u_type == 4) {
      // Cannon: Mortar Ring & Spark
      float cr = abs(sd_circle(ip, 2.7)) - 0.65;
      float cp = sd_circle(ip, 1.1);
      d_ins = min(cr, cp);
    } else if (u_type == 5) {
      // Horse: Chevron Arrow
      float c1 = sd_segment(ip, vec2(-3.8, -1.5), vec2(0.0, 1.2)) - 0.75;
      float c2 = sd_segment(ip, vec2(0.0, 1.2), vec2(3.8, -1.5)) - 0.75;
      d_ins = min(c1, c2);
    } else if (u_type == 6) {
      // Pawn: Spearhead Lozenge
      d_ins = sd_rhombus(ip, vec2(1.8, 3.0));
    }

    d_ins *= ins_scale;
    vec3 ins_col = (u_side == 0) ? vec3(0.82, 0.16, 0.14) : vec3(0.14, 0.82, 0.75);
    token_col = mix(token_col, ins_col, aafill(d_ins) * inner_fill);

    // 7. Facing Direction Indicator on Rim (pointed diamond arrow)
    vec2 fdir_norm = normalize(u_facing + vec2(0.0001));
    vec2 fperp = vec2(-fdir_norm.y, fdir_norm.x);
    vec2 fp_rel = p - fdir_norm * (r + 0.3);
    vec2 fp_rot = vec2(dot(fp_rel, fperp), dot(fp_rel, fdir_norm));
    float d_fdir = sd_rhombus(fp_rot, vec2(1.8, 2.8));
    vec3 fdir_col = (u_side == 0) ? vec3(1.0, 0.88, 0.35) : vec3(0.30, 0.95, 0.90);
    token_col = mix(token_col, fdir_col, aafill(d_fdir));

    // Outer thin boundary line
    token_col = mix(token_col, vec3(0.05, 0.05, 0.05), aaline(d_token, 1.2));

    // Blend token over background shadow / aura
    col_accum.rgb = mix(col_accum.rgb, token_col, token_fill);
    col_accum.a = max(col_accum.a, token_fill);
  }

  finalColor = col_accum;
}
