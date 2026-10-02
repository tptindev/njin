# Retro Workshop

23 original low poly assets, created in Blender 4.4.3.

9 nails (3 sizes x clean/dark/rust), 2 claw hammers, 4 wood planks, 4 steel sections, 4 corrugated sheets.

Individual models/*.glb use metre scale, identity transforms, flat normals, UVs, palette PBR materials, and custom properties. No external textures. Nail pivot is at tip (+Z toward head); hammer pivot is at handle bottom; panels/steel use bottom-centre pivots. glTF converts Blender Z-up to Y-up.

Nails: S radius 6 mm / length 80 mm; M radius 9 mm / length 130 mm; L radius 13 mm / length 200 mm. These intentionally chunky proportions improve retro game readability.

The blend contains an editable asset gallery and the original startup scene. Presentation collections are separate. Gallery nails are magnified 2.6x; individual GLBs retain original scale. The gallery GLB contains props only at display positions.

All geometry and palette materials are original; no third-party assets. Run build_assets.py inside Blender to regenerate into a new scene.
