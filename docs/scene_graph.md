# Scene graph

The `scene_graph` subsystem is an **entity/component** scene graph. It is the
authority over *what exists in the world and where* — the hierarchy of objects
and their transforms — while the rendering, audio, and other subsystems remain
the authority over *how those objects are processed*.

## Model

A scene is a tree of **nodes**. A node is the *entity*: it always has a local
transform and a set of parent/child links, and it gains behaviour by carrying
**components**.

```
scene_graph::context
└── node root
    ├── transform (intrinsic)
    ├── node "player"
    │     └── camera_component   ──handle──▶ rendering_engine camera pool
    ├── node "enemy"
    │     ├── mesh_component     ──handle──▶ rendering_engine renderable pool
    │     └── audio_component     ──handle──▶ audio subsystem pool   (future)
    └── node "light rig"  (empty group)
          └── light_component    ──handle──▶ rendering_engine light pool
```

This is composition, not inheritance: a node is not a `camera_node` *subclass*;
it is an entity that *has* a camera component. The same node can be a mesh **and**
an audio emitter **and** a light at once, which is exactly the case a typed-node
hierarchy (Godot `Camera3D`/`MeshInstance3D`, Three.js `Mesh`/`Camera`) handles
awkwardly. The reference points are Unity's `GameObject` + Components and
Unreal's `Actor` + `SceneComponent`s.

## Ownership: the node holds a handle, the subsystem pools the storage

A node does **not** store component data. For each component it carries, it
records a `component_handle` — an index plus a generation counter — into a pool
owned by a subsystem. The node owns the *handle* and drives the component's
*lifetime* (destroying the node, or calling `remove_component`, frees the pooled
slot); the subsystem owns the *data*.

Two reasons this is worth the indirection:

- **Locality / batching.** Component data stays contiguous in the subsystem that
  iterates it every frame (all meshes together, all lights together) instead of
  being scattered across a pointer tree.
- **Safety.** A handle to a freed-and-recycled slot is detected as stale on the
  next lookup (its generation no longer matches) and returns `nullptr`, rather
  than silently aliasing an unrelated object. This is the same scheme
  `rendering_engine::gpu::handle` already uses for GPU resources.

The primitive behind this is `infrastructure::pool<T>` (`infrastructure/pool.hpp`):
a generational slot pool that hands out `infrastructure::pool_handle<Tag>`.
`scene_graph::component_store` keeps one such pool per component type, keyed by
`std::type_index`, and is owned per-scene by `scene_graph::context`.

## Transforms and world matrices

A node's local transform is `rendering_engine::util::transform`. Parenting a node
(`parent.add(child)`) points the child transform's parent at the parent node, so
`transform.get_world_matrix()` composes `parent.world * local` up the chain. Scene
renderables build their per-draw model matrix from `get_world_matrix()`, so moving
a parent node moves its whole subtree.

## Status / roadmap

The model lands in stages:

1. **Foundation (done).** `infrastructure::pool` + handle; `node` as an entity
   with a type-erased `component_store`; transform-hierarchy world-matrix
   propagation. No subsystem owns component pools yet, so any plain struct can be
   a component but nothing renders off one.
2. **Render components.** `rendering_engine` exposes pools for renderables,
   cameras, and lights with create/destroy-by-handle; `mesh_component`,
   `camera_component`, and `light_component` wrap those handles, registering with
   the renderer on attach and unregistering on detach.
3. **Traversal.** A once-per-frame walk updates world matrices (dirty-flag
   propagation) and pushes them to the render components.
4. **Further subsystems** (audio, physics) adopt the same handle/pool shape as
   they appear.
