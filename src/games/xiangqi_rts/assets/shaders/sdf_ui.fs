#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

// As a njin ui_look shader (u_ui = 1) the engine sets these, and the widget's
// skin colour arrives as fragColor. Drawn by the game itself (u_ui = 0) the
// u_* uniforms below are used instead.
uniform int u_ui;
uniform vec4 uiRect;   // x, y, width, height of the widget, screen pixels
uniform float uiState; // 0 normal, 1 focused, 2 pressed, 3 disabled
uniform float uiValue; // progress 0..1
uniform float uiTime;
uniform float u_shade; // UI only: gradient bottom = skin colour * u_shade

uniform vec2 u_resolution;      // Dimensions in pixels (width, height)
uniform int u_mode;             // 0: Panel, 1: Bar, 2: Button, 3: Ring (Ability), 4: Marquee / Box, 5: Badge
uniform float u_roundness;      // Corner rounding radius in pixels
uniform float u_border_width;   // Border stroke thickness in pixels
uniform vec4 u_color_bg;        // Background color (or gradient top)
uniform vec4 u_color_bg2;       // Background secondary color (gradient bottom)
uniform vec4 u_color_border;    // Border line color
uniform float u_value;          // Progress / Fill / Cooldown ratio (0.0 .. 1.0)
uniform int u_fill_dir;         // 0: Left-to-Right, 1: Right-to-Left, 2: Circular
uniform float u_state;          // 0: Normal, 1: Hover, 2: Pressed, 3: Disabled
uniform float u_time;           // Animation time
uniform float u_has_corners;    // 1.0 to draw imperial corner brackets, 0.0 otherwise

float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sd_round_box(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

float sd_rhombus(vec2 p, vec2 b) {
  vec2 q = abs(p);
  float h = clamp((-2.0 * dot(q, b) + dot(b, b)) / max(dot(b, b), 0.0001), -1.0, 1.0);
  float d = length(q - 0.5 * b * vec2(1.0 - h, 1.0 + h));
  return d * sign(q.x * b.y + q.y * b.x - b.x * b.y);
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
  bool ui = u_ui == 1;
  vec2 res = ui ? uiRect.zw : u_resolution;
  float state = ui ? uiState : u_state;
  float value = ui ? uiValue : u_value;
  float time = ui ? uiTime : u_time;
  vec4 bg1 = ui ? fragColor : u_color_bg;
  vec4 bg2 = ui ? vec4(fragColor.rgb * u_shade, fragColor.a) : u_color_bg2;

  vec2 p = (fragTexCoord - vec2(0.5)) * res;
  vec2 b = res * 0.5;

  vec4 out_col = vec4(0.0);

  // =========================================================================
  // MODE 0: UI PANEL
  // =========================================================================
  if (u_mode == 0) {
    float r = clamp(u_roundness, 0.0, min(b.x, b.y));
    float bw = max(u_border_width, 1.0);
    float d_box = sd_round_box(p, b - vec2(bw * 0.5), r);
    float box_fill = aafill(d_box);

    // Soft drop shadow below panel
    float d_shadow = sd_round_box(p - vec2(0.0, 3.5), b, r);
    float shadow_alpha = (1.0 - smoothstep(0.0, 8.5, d_shadow)) * 0.38;
    out_col = vec4(0.0, 0.0, 0.0, shadow_alpha * (1.0 - box_fill));

    if (box_fill > 0.001) {
      // Lacquered gradient from top to bottom
      vec3 bg = mix(bg1.rgb, bg2.rgb, fragTexCoord.y);

      // Subtle vignette / inner corner shadow
      float inner_vignette = smoothstep(0.0, min(b.x, b.y) * 1.5, length(p)) * 0.15;
      bg *= (1.0 - inner_vignette);

      // Border line
      float d_border = abs(d_box + bw * 0.5) - bw * 0.5;
      float border_mask = aafill(d_border);

      vec3 panel_col = mix(bg, u_color_border.rgb, border_mask);

      // Top edge metallic sheen / glint
      float top_edge = (1.0 - smoothstep(0.0, 1.6, abs(p.y - (-b.y + bw)))) *
                       smoothstep(b.x, 0.0, abs(p.x));
      panel_col += u_color_border.rgb * (top_edge * 0.35);

      // Imperial Corner Brackets / Filigree
      if (u_has_corners > 0.5 && b.x > 30.0 && b.y > 30.0) {
        vec2 cp = abs(p) - (b - vec2(9.0));
        float arm1 = sd_box(cp - vec2(-3.5, 0.0), vec2(5.0, 1.0));
        float arm2 = sd_box(cp - vec2(0.0, -3.5), vec2(1.0, 5.0));
        float d_corner = min(arm1, arm2);
        float d_rivet = sd_circle(cp - vec2(-0.5, -0.5), 1.5);
        panel_col = mix(panel_col, u_color_border.rgb * 1.25, aafill(d_corner));
        panel_col = mix(panel_col, vec3(1.0, 0.92, 0.5), aafill(d_rivet));
      }

      out_col.rgb = mix(out_col.rgb, panel_col, box_fill);
      out_col.a = max(out_col.a, box_fill * bg1.a);
    }
  }

  // =========================================================================
  // MODE 1: PROGRESS / HEALTH BAR
  // =========================================================================
  else if (u_mode == 1) {
    float r = clamp(u_roundness, 0.5, min(b.x, b.y));
    float bw = max(u_border_width, 1.0);

    float d_frame = sd_round_box(p, b, r);
    float frame_fill = aafill(d_frame);

    if (frame_fill > 0.001) {
      float d_inner = sd_round_box(p, b - vec2(bw), max(r - bw, 0.5));
      float inner_fill = aafill(d_inner);

      // Slot background: Dark charcoal with upper depth shadow
      float upper_shadow = smoothstep(0.0, b.y * 1.2, p.y + b.y) * 0.4;
      vec3 slot_col = mix(vec3(0.07, 0.08, 0.10), vec3(0.03, 0.04, 0.05), upper_shadow);

      // Fill calculation
      float track_len = (b.x - bw) * 2.0;
      float val = clamp(value, 0.0, 1.0);
      float d_fill;

      if (u_fill_dir == 0) {
        // Left to Right
        float cut_x = -b.x + bw + track_len * val;
        d_fill = max(d_inner, p.x - cut_x);
      } else {
        // Right to Left
        float cut_x = b.x - bw - track_len * val;
        d_fill = max(d_inner, cut_x - p.x);
      }

      float fill_mask = aafill(d_fill) * inner_fill;

      // Filled bar color gradient
      vec3 bar_col = mix(bg1.rgb, bg2.rgb, (p.x + b.x) / max(2.0 * b.x, 1.0));

      // Upper gloss specular sheen (crystal reflection)
      float gloss = (p.y < 0.0) ? (0.32 * (1.0 - abs(p.y) / b.y)) : 0.0;
      bar_col += vec3(gloss);

      // Fine tick marks every 22 pixels for scale
      if (b.x > 35.0) {
        float tick_d = abs(mod(p.x + b.x, 22.0) - 1.0) - 0.5;
        bar_col = mix(bar_col, vec3(0.1, 0.1, 0.1), aafill(tick_d) * 0.35);
      }

      vec3 content_col = mix(slot_col, bar_col, fill_mask);

      // Border outline
      float d_border = abs(d_frame + bw * 0.5) - bw * 0.5;
      vec3 border_col = u_color_border.rgb;

      // Low health warning pulse
      if (val < 0.35 && bg1.r > bg1.g) {
        float pulse = 0.5 + 0.5 * sin(time * 8.0);
        border_col = mix(border_col, vec3(1.0, 0.25, 0.25), pulse * 0.7);
      }

      vec3 bar_final = mix(content_col, border_col, aafill(d_border));

      out_col.rgb = bar_final;
      out_col.a = frame_fill;
    }
  }

  // =========================================================================
  // MODE 2: BUTTON CARD (Recruitment & Actions)
  // =========================================================================
  else if (u_mode == 2) {
    float r = clamp(u_roundness, 1.0, min(b.x, b.y));
    float bw = max(u_border_width, 1.0);

    float d_btn = sd_round_box(p, b, r);
    float btn_fill = aafill(d_btn);

    // Drop shadow
    float d_shadow = sd_round_box(p - vec2(0.0, 2.5), b, r);
    float shadow_alpha = (1.0 - smoothstep(0.0, 6.0, d_shadow)) * 0.35;
    out_col = vec4(0.0, 0.0, 0.0, shadow_alpha * (1.0 - btn_fill));

    if (btn_fill > 0.001) {
      vec3 bg = mix(bg1.rgb, bg2.rgb, fragTexCoord.y);
      vec3 bcol = u_color_border.rgb;

      // Hover State (1.0)
      if (state == 1.0) {
        float hover_glow = exp(-0.04 * length(p)) * 0.25;
        bg += vec3(hover_glow * 1.1, hover_glow * 0.9, hover_glow * 0.4);
        bcol = vec3(1.0, 0.85, 0.35); // Bright gold border
      }
      // Pressed State (2.0): sunk in, lit from below
      else if (state == 2.0) {
        bg = mix(bg2.rgb, bg1.rgb, fragTexCoord.y) * 0.8 + vec3(0.10, 0.08, 0.02);
        bcol = vec3(1.0, 0.75, 0.25);
      }
      // Disabled / Cannot afford State (3.0)
      else if (state == 3.0) {
        bg *= 0.65;
        bcol *= 0.6;
      }

      float d_border = abs(d_btn + bw * 0.5) - bw * 0.5;
      vec3 btn_col = mix(bg, bcol, aafill(d_border));

      // Top light sheen
      float top_sheen = (1.0 - smoothstep(0.0, 1.5, abs(p.y - (-b.y + bw)))) * 0.3;
      btn_col += bcol * top_sheen;

      out_col.rgb = mix(out_col.rgb, btn_col, btn_fill);
      out_col.a = max(out_col.a, btn_fill);
    }
  }

  // =========================================================================
  // MODE 3: CIRCULAR MEDALLION / ABILITY COOLDOWN RING
  // =========================================================================
  else if (u_mode == 3) {
    float r_disc = min(b.x, b.y) - 2.0;
    float d_disc = sd_circle(p, r_disc);
    float disc_fill = aafill(d_disc);

    // Drop shadow
    float d_shadow = sd_circle(p - vec2(0.0, 3.0), r_disc);
    float shadow_alpha = (1.0 - smoothstep(0.0, 6.5, d_shadow)) * 0.4;
    out_col = vec4(0.0, 0.0, 0.0, shadow_alpha * (1.0 - disc_fill));

    // Outer Rotating Rays when Ready (value >= 1.0)
    if (value >= 0.999) {
      float ang = atan(p.y, p.x);
      float rays = sin(ang * 12.0 + time * 3.0) * 0.5 + 0.5;
      float d_halo = max(d_disc, 0.0);
      float halo_glow = exp(-0.35 * d_halo) * rays * 0.5;
      out_col.rgb += vec3(1.0, 0.82, 0.3) * halo_glow;
      out_col.a = max(out_col.a, halo_glow);
    }

    if (disc_fill > 0.001) {
      vec3 base_col = bg1.rgb;

      // Beveled Medallion Rim
      float d_rim = abs(sd_circle(p, r_disc - 2.2)) - 1.6;
      vec3 rim_col = u_color_border.rgb;

      // Cooldown Sweep
      float turn = fract(atan(p.x, -p.y) / 6.2831853);
      float is_ready = (value >= 0.999) ? 1.0 : step(turn, value);

      if (is_ready < 0.5) {
        // Shaded cooldown sector
        base_col *= 0.4;
        rim_col *= 0.5;
      }

      vec3 medallion = mix(base_col, rim_col, aafill(d_rim));

      // Specular shine on medallion
      float spec = pow(max(dot(normalize(-p + vec2(0.001)), normalize(vec2(-0.6, -0.8))), 0.0), 6.0) * 0.35;
      medallion += vec3(spec);

      out_col.rgb = mix(out_col.rgb, medallion, disc_fill);
      out_col.a = max(out_col.a, disc_fill);
    }
  }

  // =========================================================================
  // MODE 4: BOX SELECTION MARQUEE
  // =========================================================================
  else if (u_mode == 4) {
    float d_box = sd_round_box(p, b, 2.0);
    float inside = aafill(d_box);

    // Translucent emerald interior
    vec4 fill_col = vec4(0.2, 0.85, 0.45, 0.16);

    // Glowing border with animated marching dashes
    float d_border = abs(d_box) - 1.2;
    float border_mask = aafill(d_border);

    // Distance metric along perimeter for dash animation
    float s = (abs(p.x) > b.x - 2.0) ? p.y : p.x;
    float dash = step(0.45, fract((s + time * 36.0) / 14.0));

    vec3 bcol = mix(vec3(0.3, 0.9, 0.5), vec3(0.85, 1.0, 0.6), dash);

    out_col = fill_col * inside;
    out_col = mix(out_col, vec4(bcol, 0.95), border_mask * (0.6 + 0.4 * dash));
  }

  // =========================================================================
  // MODE 5: BADGE / EMBLEM ("VS" jewel, hotkey tag, coin)
  // =========================================================================
  else if (u_mode == 5) {
    if (u_has_corners > 0.5) {
      // Diamond / Rhombus Jewel ("VS" badge)
      float d_rhom = sd_rhombus(p, b * 0.88);
      float rhom_fill = aafill(d_rhom);

      float d_shadow = sd_rhombus(p - vec2(0.0, 2.0), b * 0.88);
      out_col = vec4(0.0, 0.0, 0.0, (1.0 - smoothstep(0.0, 5.0, d_shadow)) * 0.4);

      if (rhom_fill > 0.001) {
        vec3 jewel_bg = mix(bg1.rgb, bg2.rgb, fragTexCoord.y);
        float d_rim = abs(d_rhom + 1.2) - 1.2;
        vec3 jewel_col = mix(jewel_bg, u_color_border.rgb, aafill(d_rim));
        out_col.rgb = mix(out_col.rgb, jewel_col, rhom_fill);
        out_col.a = max(out_col.a, rhom_fill);
      }
    } else {
      // Rounded Pill Badge
      float r = min(b.x, b.y);
      float d_pill = sd_round_box(p, b, r);
      float pill_fill = aafill(d_pill);

      if (pill_fill > 0.001) {
        vec3 bg = mix(bg1.rgb, bg2.rgb, fragTexCoord.y);
        float d_rim = abs(d_pill + u_border_width * 0.5) - u_border_width * 0.5;
        vec3 pill_col = mix(bg, u_color_border.rgb, aafill(d_rim));
        out_col.rgb = pill_col;
        out_col.a = pill_fill * bg1.a;
      }
    }
  }

  finalColor = out_col;
}
