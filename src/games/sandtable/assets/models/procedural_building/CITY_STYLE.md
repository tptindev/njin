> This earlier style pass is superseded by [RETRO_STYLE.md](RETRO_STYLE.md).

# City style source kit

The building and street Blender sources now use `city-low-poly-v2`, inspired by
`assets/models/city`: simple silhouettes, flat broad faces, restrained edge detail,
solid opaque blue-grey glazing, and simple material parameters. The procedural kit
keeps its authored colours and identity; the reference's brick texture is not copied.
Retro normal/roughness image nodes are removed from the active materials.

Sources:
- `retro/modular_building_kit.blend`: 105 exportable modules.
- `../street_retro/street_retro_kit.blend`: 25 props and composed spaces.
- `retro/generate.py` and its embedded `PBK_Generator.py` run the city pass after generation.
- `../street_retro/build_street_kit.py` runs the same pass before saving its catalog.
- `city_low_poly.py` is the shared, idempotent simplification/material pass.

IDs, transforms, pivots and snap extents stay compatible. Shutters are rebuilt with
fewer corner segments and un-beveled louver solids, keeping rigid weights, hinge
angles and frame clearance. Curved bands stay closed curved strips. Shopfronts
retain 80% glazing above a 20% frame-material base. All model lettering is omitted. Wooden shutter windows have open apertures with no glass; other windows retain opaque glass.

Current catalog triangle totals (summed per asset, including composed references):

| Catalog | Previous source/export | New source | Reduction |
|---|---:|---:|---:|
| Buildings, 105 modules | 583,276 | 214,262 | 63.27% |
| Street, 25 assets | 162,108 | 46,890 | 71.07% |

These are geometry measurements, not an FPS benchmark. Reports are
`retro/city_style_report.json` and `../street_retro/city_style_report.json`.
Checks passed: shutter fit/collision and rigid animation at 11 poses across four
window variants; street mesh, bounds and walkway validation; representative renders
beside the original city models.

No GLB export was run. Existing `modules/` GLBs are the previous editions.
`source_status.json` marks both sources as requiring export. Use the existing manual
exporters with the corresponding saved source loaded, then run their validators.
If another authoring updater changes a source, run `apply_city_style.py` last
(`-- --street` for the street source) to refresh the visual profile and catalog.
