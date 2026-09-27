#version 330

in vec2 worldPos;
out vec4 finalColor;

// The whole creature is one distance field: four circles smooth-minned into a
// body, one to three tapered capsules joined to it the same way, and the same
// field thrown sideways and softened for the shadow. There is no outline
// polygon, so the silhouette is whatever the merge leaves behind.

#define LUMPS 4
#define TAILS 3 // the most any one mote grows; tailCount says how many are live

// xy is the centre, z the radius.
uniform vec4 lump[LUMPS];
uniform float blend;

// xy is the root, zw the tip; tailR is the radius at each end. Not every mote
// grows all TAILS: tailCount says how many of the arrays below are live, the
// rest is whatever was left over from a previous draw and must not be read.
uniform vec4 tailPts[TAILS];
uniform vec2 tailR[TAILS];
uniform int tailCount;
uniform float tailBlend;
// The seam the pair is minned into each other through, apart from tailBlend:
// that one welds the pair to the body, this one bridges the gap the stroke
// opens between their roots.
uniform float tailPairBlend;

uniform vec2 shadowOffset;
uniform float shadowBlur;
uniform float shadowAlpha;

uniform vec2 eyePos;
uniform vec2 pupilPos;
uniform float eyeRadius;
uniform float pupilRadius;
uniform float eyeOpen;
uniform float eyeStretch;
uniform vec2 dir;
uniform vec4 bodyColor;

// Never pure black: a shadow is the ground with the light taken off it, so
// what is left leans towards the ground's own colour.
const vec3 SHADOW_COLOR = vec3(0.075, 0.070, 0.105);
const vec3 SCLERA_COLOR = vec3(0.961, 0.961, 0.941);
const vec3 PUPIL_COLOR = vec3(0.114, 0.110, 0.102);

// Further away than any real distance, and small enough to stay exact once it
// is blended back in: floats step by 64 at 1e9, and the lerp in smin would
// hand the winning side the losing side's rounding.
#define FAR 1e4

float smin(float a, float b, float k) {
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  // Spelled a*h + b*(1-h) rather than mix(): the side that lost is multiplied
  // by an exact zero and leaves nothing behind.
  return a * h + b * (1.0 - h) - k * h * (1.0 - h);
}

// A capsule whose radius runs from ra to rb. Not an exact distance where the
// taper is steep, and it only has to be right a pixel either side of zero.
float sdTaperCapsule(vec2 p, vec2 a, vec2 b, float ra, float rb) {
  vec2 pa = p - a;
  vec2 ba = b - a;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
  return length(pa - ba * h) - mix(ra, rb, h);
}

// The eye lid: an ellipse squashed by `open` across the way the body faces.
float sdLid(vec2 p, vec2 c, float r, float open, mat2 inv, float corr) {
  float k = max(open, 0.03);
  vec2 q = inv * (p - c);
  q.y /= k;
  return (length(q) - r) * k * corr;
}

float sceneField(vec2 p) {
  float dBody = FAR;
  for (int i = 0; i < LUMPS; i++) {
    dBody = smin(dBody, length(p - lump[i].xy) - lump[i].z, blend);
  }
  float dTail = FAR;
  for (int i = 0; i < TAILS; i++) {
    if (i >= tailCount)
      break;
    dTail = smin(dTail, sdTaperCapsule(p, tailPts[i].xy, tailPts[i].zw,
                                       tailR[i].x, tailR[i].y), tailPairBlend);
  }
  return smin(dBody, dTail, tailBlend);
}

void main() {
  vec2 px = worldPos;

  float d = sceneField(px);
  float w = fwidth(d);
  float a = 1.0 - smoothstep(-w, w, d);

  vec3 col = bodyColor.rgb;

  float es = max(eyeStretch, 0.05);
  mat2 eyeInv = mat2(es) + (1.0 / es - es) * outerProduct(dir, dir);
  float eyeCorr = min(es, 1.0 / es);
  float dSclera = sdLid(px, eyePos, eyeRadius, eyeOpen, eyeInv, eyeCorr);
  float dPupil = sdLid(px, pupilPos, pupilRadius, eyeOpen, eyeInv, eyeCorr);
  float aSclera = (1.0 - smoothstep(-fwidth(dSclera), fwidth(dSclera), dSclera)) * a;
  float aPupil = (1.0 - smoothstep(-fwidth(dPupil), fwidth(dPupil), dPupil)) * aSclera;
  col = mix(col, SCLERA_COLOR, aSclera);
  col = mix(col, PUPIL_COLOR, aPupil);

  // The same field one throw away, softened over shadowBlur rather than over a
  // pixel, which is the whole difference between a shadow and a second copy.
  float dShadow = sceneField(px - shadowOffset);
  float soft = max(shadowBlur, w);
  float aShadow = (1.0 - smoothstep(-soft, soft, dShadow)) * shadowAlpha;

  // Body over shadow in one pass, so the two composite against each other here
  // and the quad hands out a single correct alpha.
  float outA = a + aShadow * (1.0 - a);
  vec3 outRGB = (col * a + SHADOW_COLOR * aShadow * (1.0 - a)) / max(outA, 1e-4);

  finalColor = vec4(outRGB, outA);
}
