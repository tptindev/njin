#pragma once

#include "city.h"

#include <array>
#include <string>
#include <vector>

// The procedural building kit (assets/models/procedural_building): 105 glTF
// modules in three styles, a rule package (rules/building_rules.json) and
// the plans the rules make. This header is the plain data side: a
// BuildingPlan and the JSON it is read from and written to, the generator
// that makes one from a request and a seed, the checks every plan must pass,
// and the assembly of a plan into module placements, runtime geometry
// (slabs with stair voids, partitions, the dogleg stair), collision boxes
// and doors. No context, no window: it runs in --pbkcheck as in the game.
// pbk_render.h draws an assembly and drives its doors.
//
// Units: metres. Plan coordinates have their origin at the front left corner
// of the footprint: x along the front, y from the front to the back, z up
// (rules/README.vi.md). A building in the city puts the plan on its box with
// `placement` (center, angle in degrees, the box's axis_x / axis_y).

namespace sandtable::city::pbk {
using namespace njin;

inline constexpr i32 rule_version = 1;
inline constexpr f32 bay = 2.0f;
inline constexpr f32 storey = 3.0f;
inline constexpr f32 ext_wall = 0.2f;
inline constexpr f32 partition = 0.12f;
inline constexpr f32 slab = 0.2f;

using polygon = std::vector<vec2>;

// [x0, y0] - [x1, y1], plan metres.
struct box2 {
  f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  f32 w() const { return x1 - x0; }
  f32 h() const { return y1 - y0; }
  bool contains(vec2 p, f32 eps = 0.0f) const {
    return p.x >= x0 - eps && p.x <= x1 + eps && p.y >= y0 - eps && p.y <= y1 + eps;
  }
};

// --- The plan (rules/building_plan.schema.json) --------------------------------

struct room {
  std::string id, type;
  polygon poly;
  f32 area = 0.0f;
  std::vector<std::string> daylight_edges;
  f32 furniture_target = 0.2f;
};

struct floor_plan {
  i32 level = 0;
  f32 elevation = 0.0f;
  std::vector<room> rooms;
  std::vector<polygon> voids; // holes in this floor's slab (the stair below)
};

struct portal {
  std::string id, from, to;
  i32 floor_from = 0, floor_to = 0;
  vec2 center{};
  vec2 normal{};          // the wall's normal (the axis direction, not signed by from/to)
  std::string axis;       // x, y, tangent, vertical
  std::string kind;       // open, door, stair_link
  std::string module_id;  // the exterior door's module, empty inside
  f32 aperture = 1.05f, clear = 0.9f, height = 2.35f;
  bool exterior() const { return from == "outside" || to == "outside"; }
  bool vertical() const { return kind == "stair_link"; }
};

struct stair_core {
  bool on = false;
  std::string id = "main", type = "dogleg"; // or "straight": lower_flight is the flight,
                                             // lower/upper_landing its foot and its top
  std::vector<std::string> room_ids;
  f32 clear_width = 1.2f, riser = 0.2f, tread = 0.25f;
  i32 split[2] = {8, 7};
  box2 lower_flight, upper_flight, lower_landing, turn_landing, upper_landing;
  std::vector<vec3> walking_line; // x, y, z above the floor it starts on
};

struct aperture {
  i32 floor = 0;
  std::string room;
  vec2 center{}, inward{};
  std::string module_id;
  f32 glazed = 1.5f;
};

struct plan {
  std::string schema = "sandtable.building-plan.v1";
  i32 rule_version = pbk::rule_version;
  std::string kit_revision = "unified-kit-v3";
  u32 seed = 0;
  std::string archetype, style = "Modern", space_preset = "spacious";
  f32 width = 0.0f, depth = 0.0f;
  std::string shape = "Rectangle";
  polygon outer;
  std::vector<polygon> holes;
  f32 radius = 0.0f; // the rounded corner's, 0 for none
  std::vector<std::string> frontages;
  std::vector<floor_plan> floors;
  std::vector<portal> portals;
  stair_core stair;
  std::vector<std::string> required_modules;
  std::vector<std::string> runtime_geometry;
  std::string provenance;
  std::vector<aperture> apertures;
  // What the generator was given and chose, so the same house comes out of
  // the same request (written to JSON as "generator").
  json_value generator;

  const room *find_room(i32 floor, const std::string &id) const;
  i32 storeys() const { return static_cast<i32>(floors.size()); }
};

// --- The request (rules/building_request.schema.json) --------------------------

struct request {
  u32 seed = 1;
  i32 rule_version = pbk::rule_version;
  std::string archetype = "detached_spacious";
  std::string style;        // empty: picked by district weights
  std::string district = "residential";
  std::string space_preset; // empty: the archetype's
  f32 width = 12.0f, depth = 10.0f;
  i32 floors = 2;
  std::vector<std::string> road_sides{"south"};
  f32 radius = 4.0f; // CornerShopHouseRounded
};

// --- The rules (rules/building_rules.json) ---------------------------------------

struct room_rule {
  f32 min_area = 0, target_area = 0, min_width = 0;
  bool daylight = false;
  std::string privacy, legacy;
};

struct archetype_rule {
  std::string shape;
  i32 width_bays[2] = {1, 1}, depth_bays[2] = {1, 1}, floors[2] = {1, 1};
  std::vector<std::string> ground, upper;
  std::vector<std::string> frontages;
  std::string preset = "spacious";
  bool rear_blank = false;
  std::vector<f32> radii;
  std::vector<std::pair<std::string, f32>> roofs; // roof_weights
  // How the rooms go (pbk_gen.cpp): band (front rooms, a corridor, back
  // rooms), tube (one room across a narrow lot, one behind another), corridor
  // (units either side of a corridor), hall (one open floor, service rooms
  // along the back).
  std::string layout = "band";
  std::string stair = "dogleg";  // or straight (the kit's Stair)
  bool party_walls = false;      // the sides lean on neighbours: no windows
  bool free_standing = false;    // windows on every side
  bool shop_frontage = false;    // the street front's ground floor all shop glass
};

struct preset_rule {
  f32 corridor_min = 1.4f, corridor_target = 1.6f, furniture_max = 0.25f, area_scale = 1.0f;
};

struct rules {
  bool loaded = false;
  i32 version = 0;
  std::string kit_revision;
  i32 retry_count = 32;
  u32 salt_footprint = 101, salt_program = 211, salt_layout = 307, salt_appearance = 401, salt_facade = 503,
      salt_furniture = 601;
  std::vector<std::pair<std::string, room_rule>> rooms;
  std::vector<std::pair<std::string, archetype_rule>> archetypes;
  std::vector<std::pair<std::string, preset_rule>> presets;
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, f32>>>> district_styles;
  std::vector<std::pair<std::string, std::array<rgba, 4>>> palettes; // wall, trim, frame, roof (linear)
  f32 window_density[2] = {0.55f, 0.9f};
  f32 actor_radius = 0.3f, wall_margin = 0.1f, headroom = 2.2f, door_clear = 0.9f, portal_keepout = 0.35f;
  f32 glazing_ratio = 0.08f, wet_tolerance = 0.1f;
  f32 stair_flight = 1.2f, stair_landing = 1.4f, stair_core_width = 2.84f, stair_core_length = 4.8f;
  i32 risers = 15;

  const room_rule *room(const std::string &type) const;
  const archetype_rule *archetype(const std::string &name) const;
  const preset_rule *preset(const std::string &name) const;
  const std::array<rgba, 4> *palette(const std::string &style) const;
};

// The kit's folder, relative to the game's working directory: the retro
// edition, whose manifest, modules, textures and rules all sit in it (the
// manifest's paths are relative to it).
inline constexpr const char *kit_dir = "assets/models/procedural_building/retro";
// The original kit's rule package: only the archetypes and room types the retro
// package has not got are read from it (load_rules).
inline constexpr const char *legacy_rules_path = "assets/models/procedural_building/rules/building_rules.json";

// Reads rules/building_rules.json once (later calls return the same).
const rules &load_rules();

// Same as rules/cpp_contract.hpp::sub_seed and render_common.cpp::mix.
u32 sub_seed(u32 seed, u32 salt);

// --- JSON -------------------------------------------------------------------------

bool plan_from_json(const json_value &j, plan &out, std::string *error);
json_value plan_to_json(const plan &p);
bool load_plan(const char *path, plan &out, std::string *error);
bool request_from_json(const json_value &j, request &out);

// --- Generating ------------------------------------------------------------------

// What came of a request: a plan, or NoFit and why (every rule that failed on
// the best try, the tries used).
struct gen_result {
  bool ok = false;
  plan p;
  i32 tries = 0;
  std::vector<std::string> reasons;
};

// The plan for `req`, from its seed. Never shrinks a hard minimum: when no try
// in rules::retry_count fits, the result is NoFit.
gen_result generate(const request &req);
// Prints why each try failed (--pbkcheck's probe), and writes the walkable
// raster of the first failed try to pbk_out/.
inline bool gen_trace = false;
struct assembly;
void dump_nav(const plan &p, const assembly &as, const char *prefix);

// --- Checks ----------------------------------------------------------------------

struct check_report {
  std::vector<std::string> errors;   // hard rules broken
  std::vector<std::string> warnings; // soft targets missed
  f32 score = 0.0f;                  // 0..1, soft targets met
  bool ok() const { return errors.empty(); }
};

// Every hard rule of building_rules.json::validation that the data can show:
// rooms inside the footprint and apart, areas and widths, portals on real
// walls, all rooms reachable on the eroded walkable raster with the doors as
// they are, the stair's headroom against the slabs above, wet stacks lined
// up, daylight. With its assembly also: door sweeps clear of walls, props
// and each other, the headroom over the stair against every solid above,
// door openings free of collision, module ids, the curve's sockets meeting,
// no module laid twice in a bay, props within the coverage limit.
check_report check_plan(const plan &p, const assembly *as = nullptr);

// --- Walkable raster ---------------------------------------------------------------

// One floor's walkable ground on a square grid, eroded by the actor's radius
// and the wall margin: what an actor's centre may stand on.
struct nav_floor {
  f32 cell = 0.05f;
  i32 nx = 0, ny = 0;
  std::vector<u8> walk; // 1 walkable
  bool at(vec2 p) const;
  i32 index(vec2 p) const;
  vec2 center(i32 i) const { return {(static_cast<f32>(i % nx) + 0.5f) * cell, (static_cast<f32>(i / nx) + 0.5f) * cell}; }
};

// The doors that are shut (portal ids): a shut or locked door is a wall. An
// open door's leaf, and the assembly's props, stand in the way.
nav_floor build_nav(const plan &p, i32 floor, const std::vector<std::string> &shut, const assembly *as);

// Rooms reachable from the street door on foot, stairs included, with the
// doors in `shut` closed. Ids of the rooms not reached.
std::vector<std::string> unreachable_rooms(const plan &p, const std::vector<std::string> &shut, const assembly *as);

// --- Assembly ------------------------------------------------------------------------

// A module of the kit at its place: `pos` is where the module's origin goes
// (plan metres, `z` up), `yaw` its turn about +Y in degrees in the plan's
// own frame (see placement::yaw_of). `floor` -1 for the roof.
struct module_place {
  std::string id;
  vec3 pos{};
  f32 yaw = 0.0f;
  i32 floor = 0;
  i32 door = -1; // an animated door: its index in assembly::doors
  bool inside = false; // inside the house (the Stair): shown on an open floor too
  bool dressing = false; // drawn without its substrate and floor band (the shell is laid round it)
  i32 shutter = -1;      // a window with wooden shutters: its index in assembly::shutters
};

// A solid box, plan metres: `c` its centre on the plan, `z0`..`z1` its height,
// `half` along `angle` (degrees, plan frame) and across.
struct solid {
  vec2 c{};
  vec2 half{};
  f32 angle = 0.0f;
  f32 z0 = 0.0f, z1 = 0.0f;
  i32 floor = 0;
  u8 kind = 0; // solid_kind
  rgba color{1, 1, 1, 1};
};
// sk_exterior: the kit's modules draw it, the box is collision only;
// sk_facade: outside wall the modules do not cover, drawn and solid.
// sk_collision: solid but not drawn (a kit module draws it: the Stair's steps).
// sk_shell: the welded wall ring, floor band and parapet the kit's generator
// lays round its dressing (drawn, not solid: sk_exterior is the collision).
enum solid_kind : u8 { sk_exterior = 0, sk_partition, sk_slab, sk_stair, sk_rail, sk_lintel, sk_leaf, sk_prop, sk_facade,
                       sk_collision, sk_shell };

// A door that opens: the street door (a DoorRigged module, its clip drives
// it) or a door between rooms (a leaf made here, turned by code).
struct door {
  std::string portal;
  bool rigged = false;
  std::string module_id;
  i32 floor = 0;
  vec2 hinge{};       // plan metres
  f32 closed_angle = 0.0f; // the leaf's direction from the hinge when shut, degrees in the plan frame
  f32 swing = 90.0f;  // degrees it turns to open, signed (toward the inside)
  f32 width = 0.95f, thickness = 0.04f, z0 = 0.03f, z1 = 2.32f;
  f32 leaf_off = 0.0f; // across the leaf's line from the hinge to its middle
};

// A piece of furniture: a model of the old interior kit (assets/models/interior)
// and the footprint it takes, plan metres.
struct furniture {
  std::string model;
  i32 floor = 0;
  std::string room;
  vec2 c{};
  vec2 half{}; // along angle, across
  f32 angle = 0.0f;
  f32 height = 0.8f;
};

// A window whose wooden shutters open and shut (the kit's Indochine windows):
// the module placed, every rig of it moved together by the clip.
struct shutter {
  std::string module_id;
  i32 module = -1; // index in assembly::modules
  i32 floor = 0;
};

struct assembly {
  std::vector<module_place> modules;
  std::vector<solid> solids;     // runtime geometry and collision both
  std::vector<door> doors;
  std::vector<shutter> shutters;
  std::vector<furniture> furniture;
  std::vector<std::string> notes;  // what was approximated, for the check report
};

// The modules, runtime geometry, doors and props of a plan. Facade details
// (windows beyond the required ones) are drawn from the plan's facade seed,
// so the same plan always assembles the same way.
assembly assemble(const plan &p);

// Placing a plan on the table: what rules/README.vi.md calls the adapter.
struct placement {
  vec2 center{};     // table, world units
  f32 angle = 0.0f;  // degrees, as building::box.angle
  f32 width = 0.0f, depth = 0.0f; // the plan's, metres
  vec2 to_table(vec2 local) const;
  vec2 dir_to_table(vec2 d) const;
  vec2 to_local(vec2 table) const;
  // A module's yaw in 3D (degrees about +Y) for a plan-frame yaw.
  f32 yaw_of(f32 plan_yaw) const;
  vec3 to_render(vec3 local) const; // world 3D units (view.h to3d)
};

// The yaw (plan frame) that turns a module's +Z to point along `out` (plan
// metres, unit), and its +X along the wall.
f32 yaw_facing(vec2 out);
// A module's local point (glTF metres: x, y up, z out) in the plan frame.
vec3 module_to_plan(vec3 m, vec3 origin, f32 yaw);

// --- The kit's manifest ------------------------------------------------------------

struct module_info {
  std::string id, path, family, style;
  std::string revision;             // the kit revision the file was exported at
  bool dressing_only = false;       // assembly_mode dressing_only: never laid alone as a wall
  vec3 lo{}, hi{};                  // bounds_gltf_m
  std::vector<std::pair<std::string, vec3>> sockets; // sockets_gltf_m
  f32 right_tangent = 0.0f;         // degrees
  f32 radius = 0.0f, arc_angle = 0.0f;
  bool animated = false;
  std::string anim_name;
  f32 anim_duration = 0.0f, open_time = 0.0f, closed_time = 0.0f;
  f32 open_angle = 0.0f;
  i32 rig_count = 0;
  bool loop = false;
  bool shutters = false;            // a window with wooden shutters (Shutter_CloseOpen), no glass behind
  vec3 socket(const char *name) const;
};

struct manifest {
  bool loaded = false;
  std::string kit_revision;
  f32 render_scale = 0.1875f;
  std::vector<module_info> modules;
  const module_info *find(const std::string &id) const;
};

const manifest &load_manifest();

// A file of the kit by the manifest's path (relative to the manifest's folder).
std::string kit_path(const std::string &rel);

// The timeline of a door's or a window's clip, from the manifest: the clip
// goes from its first pose at 0 to the other at t1, holds a second, and
// comes back from t1 + 1 to t1 + 2. Door_OpenClose starts shut (open at
// 23/24, shut again at 71/24); Shutter_CloseOpen starts open (shut at 23/24,
// open again at 71/24).
struct door_clip {
  f32 open_from = 0.0f, open_to = 23.0f / 24.0f;
  f32 close_from = 47.0f / 24.0f, close_to = 71.0f / 24.0f;
  f32 shut_pose = 0.0f, open_pose = 23.0f / 24.0f; // clip times of the two poses
  f32 duration = 95.0f / 24.0f;
};
door_clip clip_of(const module_info *mi);

// A 4x4 transform, column-major as glTF: m[12..14] is the translation.
struct mat4 {
  f32 m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
mat4 operator*(const mat4 &a, const mat4 &b);
vec3 mat4_point(const mat4 &m, vec3 p);
mat4 mat4_affine_inverse(const mat4 &m);
// Turn about +Y, degrees (as transform3d's y); a move; a uniform scale.
mat4 mat4_yaw(f32 deg);
mat4 mat4_move(vec3 v);
mat4 mat4_scale(f32 s);
// The turns of a rotation (and scale-free) matrix about x, y and z, degrees,
// in njin's transform3d order (z first, then x, then y).
vec3 mat4_euler(const mat4 &m);
// The turn about +Y of a matrix's +X axis, degrees (a hinge's leaf).
f32 mat4_yaw_of(const mat4 &m);

// The moving leaves of an animated module: a door's leaf, a window's two
// shutters, the corner window's four (two rigs). Every skinned mesh node of
// the GLB is a leaf, moved by the joint its vertices hang on, whatever its
// name, in whichever skin of the file; the clip is sampled for every joint
// it moves (translation, rotation, scale), its parents' too.
struct rig_leaf {
  std::string node;  // the mesh node's name (model_load's only_nodes)
  i32 skin = 0;      // which rig of the file
  std::string joint; // the bone that moves it
  vec3 lo{}, hi{};   // its vertices' bounds where the engine's loader leaves them (glTF metres)
};

struct module_rig {
  bool ok = false;
  std::string clip;
  i32 rigs = 0;
  std::vector<rig_leaf> leaves;
  // Leaf `k` at clip time `t`: from where the engine's loader leaves its
  // vertices (each node's rest transform applied) to where the clip puts
  // them, module metres.
  mat4 leaf_at(i32 k, f32 t) const;
  // Leaf `k`'s turn about +Y at `t` from its turn at `from`, degrees.
  f32 turn(i32 k, f32 t, f32 from) const;

  // The file's nodes and the clip's tracks (pbk_data.cpp).
  struct track {
    i32 node = -1, path = 0; // path 0 translation, 1 rotation, 2 scale
    bool step = false;
    std::vector<f32> times, values;
  };
  struct node_rest {
    vec3 t{}, s{1, 1, 1};
    f32 q[4] = {0, 0, 0, 1};
    i32 parent = -1;
  };
  std::vector<node_rest> nodes;
  std::vector<track> tracks;
  std::vector<i32> leaf_joint;     // node index of each leaf's joint
  std::vector<mat4> leaf_bind;     // each leaf's inverse bind matrix
  std::vector<mat4> leaf_unload;   // inverse of each leaf node's rest world (undoes the loader)
  mat4 world(i32 node, f32 t) const;
};
// The rig of `module_id`'s file, read once (thread-safe: the generator's
// worker reads it too); `ok` false for a module with no skinned mesh.
const module_rig &module_rig_of(const std::string &module_id);

// What a GLB file holds, from its own JSON chunk (no engine needed): for the
// check that the game loads the whole hierarchy.
struct glb_summary {
  bool ok = false;
  i32 nodes = 0, meshes = 0, materials = 0, skins = 0, joints = 0;
  std::vector<std::string> animations;
  bool root_identity = false;
  vec3 lo{}, hi{}; // positions' bounds, node transforms applied
  std::vector<std::string> mesh_nodes; // names of the nodes with a mesh, in file order
  i32 primitives = 0, with_tangent = 0;
  i32 incomplete = 0; // primitives without normals, or a textured one without UVs or the tangents its normal map needs
  i32 images = 0, embedded_images = 0; // embedded: in the GLB's own buffer, no file beside it
  i32 glass = 0;                       // materials with KHR_materials_transmission
  std::vector<i32> glazing_materials; // source material indices, including opaque *_glass
  std::vector<std::string> textured;   // materials with a base colour texture
};
glb_summary read_glb(const std::string &path);


// --- The footprint and its facade ------------------------------------------------------

// The footprint shapes this runtime builds: a rectangle, or one with its
// front right corner rounded (CornerShopHouseRounded). Courtyard, U and T
// need custom tiles (rules/README.vi.md) and are refused.
bool shape_supported(const std::string &shape, std::string *why);

// The outline `inset` metres in from the outer face (0: the outer face
// itself), from the plan's shape and radius; the arc in `steps` pieces.
polygon footprint_inset(const plan &p, f32 inset, i32 steps = 48);

// Whether a side may have windows: a street front, or a free side; party
// walls and blank rears may not.
bool side_glazable(const plan &p, const std::string &side);

// One bay of the facade on one floor: a 2 m straight bay, or one piece of
// the rounded corner. `origin` and `yaw` place the module the kit's way
// (module +X along the wall, +Z out of it); `center` is the middle of its
// outer face, `out` its outward normal at the middle.
struct bay_slot {
  std::string side; // south, east, north, west, rounded_corner
  i32 index = 0;    // along the side, in the order the modules run
  i32 count = 0;    // bays on this side
  bool arc = false;
  vec2 origin{}, end{}, center{}, out{};
  f32 yaw = 0.0f, end_yaw = 0.0f;
  bool first = false, last = false; // at the start / end of its side (a corner)
};
// The bays of every side, in the order modules chain round the building.
std::vector<bay_slot> exterior_bays(const plan &p);

// The room a bay of floor `floor` opens onto (its inside, 0.4 m in), or null.
const room *room_behind(const plan &p, i32 floor, const bay_slot &b);

// The requests for building `id` of the town, best first: its box in whole
// 2 m bays and its floors, for each archetype of the rules that suits what it
// is (a shop on a street: shop_house, then tube_shop_house; a house: detached,
// townhouse, tube_house; flats, hotels, schools, workshops, warehouses, the
// market: theirs). The first the generator fits is the house. Empty, with
// why, when none suits; `center` is where the plan's middle goes.
std::vector<request> requests_for_building(const city_map &map, i32 id, vec2 &center, std::string *why = nullptr);

// --pbkcheck (pbk_tool.cpp): the manifest, the example plans and the
// generator over `seeds` seeds per archetype. Returns the failures.
i32 run_pbk_check(i32 seeds, bool verbose);

// The kit's colours are linear (glTF baseColorFactor, rules
// appearance.palettes); njin's 3D shading takes sRGB colours like the rest of
// the game's: this is the one conversion, done once where the colours come in.
rgba linear_to_srgb(rgba c);

// --- Geometry helpers ---------------------------------------------------------------

f32 polygon_area(const polygon &p);
bool point_in_polygon(const polygon &p, vec2 q);
// Distance from q to the polygon's outline.
f32 distance_to_outline(const polygon &p, vec2 q);
// `p` clipped to the convex polygon `clip` (Sutherland-Hodgman).
polygon clip_convex(const polygon &p, const polygon &clip);
polygon rect_poly(const box2 &b);
box2 bounds_of(const polygon &p);
// The narrowest clear width of a room: the smallest side of its bounds for
// a rectangle, an inscribed estimate for others.
f32 clear_width(const polygon &p);

} // namespace sandtable::city::pbk
