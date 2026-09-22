# Assets

The asset layer (`rendering_engine/assets/`) is the `asset_cache` — the
deduplicating, reference-counted store for textures, fonts and meshes
described in `CLAUDE.md` — plus the loaders that feed it. This page covers the
one file importer the engine has: glTF 2.0.

## glTF import

`rendering_engine::load_gltf(path)` (`assets/gltf_importer.hpp`) reads a
`.gltf` or `.glb` file through the vendored single-header
[cgltf](https://github.com/jkuhlmann/cgltf) parser (`vendor/cgltf/cgltf.h`,
MIT) and returns a `gltf_model`: the file's node tree, its TRIANGLES
primitives as cached `mesh_asset`s, one `standard_material` per glTF material
and one cache `texture_asset` per glTF texture. External `.bin` and image
files resolve relative to the glTF file, GLB binary chunks and base64 data
URIs are handled in memory, and a parse / buffer / validation failure throws
`std::runtime_error` after `LOG_ERR`. Only static geometry is imported: skins,
animations and morph targets are ignored with a warning (issue #221), as are
Draco-compressed and non-triangle primitives, which are skipped.

**Geometry.** Every primitive becomes `vertex_position_uv_normal_tangent`
records — the layout `standard_material` reads — so any file draws under the
PBR material without a per-format pipeline: `TEXCOORD_0` defaults to zero when
absent, a primitive without normals is unwelded and given flat face normals
(the spec says its tangents must then be ignored), and tangents come from the
file's `TANGENT` attribute or, failing that, from `generate_tangents`
(`gltf_import_options::generate_missing_tangents`, on by default). Indices are
widened from u8 / u16 / u32 to u32 (a non-indexed primitive is numbered
0..n-1). Each primitive is routed through `asset_cache::get_or_create_mesh`
under `"gltf:<canonical path>#mesh<i>/prim<j>"`, so loading the same file
twice shares every upload while the first model is alive. The POSITION
accessor's `min` / `max` is forwarded as `mesh_data::bounds`, so the cached
`mesh_asset::bounds` (which the passes frustum-cull with) is the file's
authoritative box and the cache only scans the positions when a file omits
them.

**Materials and textures.** Each glTF material maps onto `standard_material`:
`baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`
(times `KHR_materials_emissive_strength`) and the base colour, normal,
metallic-roughness and emissive maps. glTF stores roughness in the **G** and
metalness in the **B** channel of one packed texture, whereas
`standard_material` samples `.r` of two separate maps, so the importer splits
the packed image on the CPU into a metallic map and a roughness map (each
channel replicated into RGB). This is the interim arrangement until the
material takes a packed ORM map (issue #210); the occlusion channel is
dropped for the same reason. Base-colour and emissive images upload as sRGB
(`gpu::color_space::srgb`, overridable for base colour through
`gltf_import_options::base_color_space`) and normal / metallic-roughness
images as linear, both into the material's own textures (via the
`set_*_map(image, space)` setters) and into the cache's `gltf_model::textures`
(keyed by file path for image files, by `"gltf:<path>#image<i>"` for embedded
ones), so a texture shared by several models or materials decodes once per
colour space. Material creation itself goes through the
`gltf_material_factory` interface: production uses
`gltf_standard_material_factory` (`assets/gltf_material_factory.cpp`), the
only importer translation unit that reaches the live renderer, while the unit
tests pass a recorder and run the whole import headless against the fake
device. The factory hands materials back as `std::shared_ptr` so their deleter
is bound renderer-side and no headless translation unit ever instantiates
`delete` on a `standard_material` (the UBSan vptr check would otherwise need
its typeinfo, which only `standard_material.cpp` provides). Alpha modes,
double-sided rendering, specular-glossiness materials, `KHR_texture_transform`
and non-zero `texCoord` indices are not supported and are warned about per
material.

**Scene graph and coordinate system.** `runtime::instantiate_gltf(model,
parent)` (`runtime/gltf_instantiate.hpp`) creates one caller-owned
`runtime::node` per glTF node reachable from the default scene's roots,
parents them under `parent`, applies each node's name and local TRS (a node
authored as a matrix is decomposed), and attaches a `mesh_component` per
primitive — a node with several primitives gets one child node per primitive,
named `"<node>/primitive<k>"`, since a node holds one component of a type.
Primitives without a material draw with the model's shared
`default_material` (glTF's default: white, metallic 1, roughness 1). glTF is
+Y up and the engine +Z up, so every **root** node's pose is pre-rotated by
+90° about X: glTF +Y becomes +Z and glTF +Z (the asset's front) becomes -Y.
A pure rotation commutes past a translation (`R * T(t) = T(R t) * R`), so
this folds into the root's own TRS and no extra node is inserted. The
returned nodes must be destroyed before the `gltf_model` (their mesh
components draw with its materials) and both before the engine quits.

`external/gltf_demo_module.cpp` (not compiled by default; see
`external/CMakeLists.txt`) is the worked example: it loads the file named by
the `ALPHAENGINE_GLTF` environment variable, instantiates it under the scene
root, lights it and places the camera in front of its bounds.
