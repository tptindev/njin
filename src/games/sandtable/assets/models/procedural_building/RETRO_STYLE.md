# Retro low-poly authoring kit

Reference: the local `assets/models/city` models and
[Quaternius Downtown City MegaKit](https://quaternius.com/packs/downtowncitymegakit.html).
The pack's shape and modular construction approach are references; no downloaded
models or textures are copied. Sources retain existing kit IDs; active folders are `retro/` and `../street_retro/`.

The sources use hard edges, square frames, flat shading and simple material colours.
Retro normal/roughness textures and geometric fillets are omitted. Round street props
use eight sides; tubes use six sides; spherical food/props use an 8-by-4 mesh.

Exterior walls are render surfaces, not solid prisms. A plain module's substrate is
one quad/two triangles. Openings are real holes. Curved substrates have one radial
layer and faceted curves. Generated buildings have one welded exterior wall mesh,
with a `storey` face attribute. RoofSlope is one quad, Gable is one triangle.

Interior wall rendering is separate. Collision remains plan-derived with a 0.2m
wall thickness; never use the thin rendering mesh's AABB as a solid wall collider.
The JSON rule and schema describe this contract for game integration.

Frames meet the wall at depth zero. Window panes and shopfront kick panels share
the same depth and exact opening edges. Shopfronts keep 80% glass / 20% base, and
the base uses the frame's material. Door reveal surfaces connect to the existing
door leaf without moving its rig. Shutters keep only their working clearances.
Shutter windows have no glass; other windows retain opaque glazing. No lettering.

Source checks passed: 54 structural module surfaces and 11 sample-building meshes;
18 frame/pane contact cases; shutter fit, direction and collision at 11 poses per
variant; 25 street props, bounds and walkway; rule schema and generator syntax.
GLB roundtrip checks passed for 105 building modules and 25 street props, including
13 animated modules and 65 sampled animation poses. No game FPS benchmark was run.

No GLB export is automatic. Load the corresponding saved `.blend`, run the existing
manual exporter, then its validator. Street exports are current. The building
source is now v18; its GLBs remain the validated v17 export until the next manual
export. `source_status.json` records this pending state.

Building regeneration embeds `city_low_poly.py` and `retro_low_poly.py` into
`retro/generate.py` / `PBK_Generator.py`. Street regeneration imports both helpers.
Run `apply_city_style.py` last after other source edits (`-- --street` for street).

Stair repair (source revision v16): the three Stair modules are closed, single
meshes with 15 real treads (200 mm rise / 250 mm run), 64 vertices and 124
triangles each. The style pass protects their silhouette from box replacement.
`stair_validation.json` checks all tread heights, volume, manifold edges and
reopened generator idempotence. The current GLBs include this stair repair. The earlier
game runtime validation above was run on revision v15.

Source revision v17 adds balanced 40 mm wooden crossbars to flat glazed
windows only. The cross is centered in the opening, touches its frame and
uses its frame material. Flat faces of corner windows inherit the same cross
through dressing; curved glazing and shutter windows have no crossbars.
`window_cross_validation.json` records topology, fit and generator checks.
Manual export and roundtrip validation completed for v17.
`tools/export_all_retro_glb.ps1` exports all 196 building/street/railing GLBs.

Source revision v18 simplifies each balcony's 14 balusters, slab, threshold and
two handles into individual 12-triangle boxes. Their original local bounds,
materials, transforms and dressing links are retained. The full-height access
doors, glazing and facade aperture retain their existing shape.

Each of the three balcony modules falls from 14,734 to 494 triangles (96.65%).
Across the 105 exportable building modules, the total falls from 95,801 to 53,081
triangles (44.59%). Balconies now account for 2.79% of that total, down from 46.14%.
These are source geometry counts, not FPS measurements. `balcony_validation.json`
checks exact detail bounds, closed topology, dressing links and generator reuse;
the 18 frame/pane contact checks also passed. No v18 GLB export has been run.
