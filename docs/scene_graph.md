# Scene graph

The scene graph (part of the `runtime` module) is an **entity/component** scene
graph. It is the
authority over *what exists in the world and where* — the hierarchy of objects
and their transforms — while the rendering, audio, and other subsystems remain
the authority over *how those objects are processed*.

## Model

A scene is a tree of **nodes**. A node is the *entity*: it always has a local
transform and a set of parent/child links, and it gains behaviour by carrying
**components**.

```
runtime::context
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
hierarchy (Godot `Camera3D`/`MeshInstance3D`) handles
awkwardly. The reference points are Unity's `GameObject` + Components and
Unreal's `Actor` + `SceneComponent`s.

## Ownership: the node holds a handle, the subsystem pools the storage

A node does **not** store component data. For each component it carries, it
records a `component_handle` — an index plus a generation counter — into a pool
owned by a subsystem. The node owns the *handle* and drives the component's
*lifetime* (destroying the node, or calling `remove_component`, frees the pooled
slot); the subsystem owns the *data*. Should the store itself die first, it
dispatches `on_destroy` to every component still in it before freeing them.

Two reasons this is worth the indirection:

- **Locality / batching.** Component data stays contiguous in the subsystem that
  iterates it every frame (all meshes together, all lights together) instead of
  being scattered across a pointer tree.
- **Safety.** A handle to a freed-and-recycled slot is detected as stale on the
  next lookup (its generation no longer matches) and returns `nullptr`, rather
  than silently aliasing an unrelated object. This is the same scheme
  `rendering_engine::gpu::handle` already uses for GPU resources.

The primitive behind this is `core::pool<T>` (`core/pool.hpp`):
a generational slot pool that hands out `core::pool_handle<Tag>`.
`runtime::component_store` keeps one such pool per component type, keyed by
`std::type_index`, and is owned per-scene by `runtime::context`.

## Transforms and world matrices

A node's local transform is `rendering_engine::util::transform`. Parenting a node
(`parent.add(child)`) points the child transform's parent at the parent node, so
`transform.get_world_matrix()` composes `parent.world * local` up the chain. Scene
renderables build their per-draw model matrix from `get_world_matrix()`, so moving
a parent node moves its whole subtree.

World matrices are **cached**. Each transform tracks a local-change version and a
world version; `get_world_matrix()` rebuilds its cached world matrix only when its
own local components changed or an ancestor's world version advanced. A static
hierarchy therefore costs no repeated matrix multiplies, and the scheme stays
correct for the renderable-transform-parented-to-node case (the renderable polls
the node transform's world version) without push-based invalidation.

## Node conveniences

- **`name` + `find(name)`** — depth-first search of a subtree (self included).
- **`active`** — `set_active(false)` disables a node and, by inheritance, its
  subtree: `update_subtree` skips it and components are told to hide via the
  `on_active_changed(node&, bool)` hook (`mesh_component` unregisters its
  model from the renderer, `light_component` takes its light out of the light
  registry, `camera_component` detaches its camera). `is_effective_active()`
  folds in ancestors, and the traversal tests exactly that, so a node under a
  disabled parent neither updates nor draws even though its own flag is set.
  A node detached from a disabled parent (`remove`, or the parent dying) is a
  root again and answers to its own flag.
- **Cycle rejection** — `add(child)` refuses, with an error logged, to link a
  node under itself or under one of its own descendants.
- **World-space helpers** — `world_position()`, `set_world_position()` (solves for
  the local position under the current parent), and `look_at(target)` (exact when
  ancestors are unrotated).

## Lifecycle

### Component hooks

A component is any movable struct. It takes part in the node's lifecycle by
defining whichever of these member functions it needs; the store detects each
one with `if constexpr (requires ...)`, so plain-data components define none.

| Hook                             | Called by                                             | When                                                                                                                                      |
| -------------------------------- | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `on_attach(node&)`               | `node::add_component`                                 | Once, right after the value is pooled, with the owning node. Wire external registrations here.                                            |
| `on_update(node&)`               | `node::update_subtree` (from `context::update`)       | Once per frame, parents before children, only while the node is effectively active.                                                       |
| `on_active_changed(node&, bool)` | `node::set_active`, `add`, `remove`, `~node`          | Whenever the node's *effective* active state flips (own flag, or an ancestor's). Also on `add_component` to a disabled node, with `false`. |
| `on_destroy()`                   | `node::remove_component`, `~node`, `~component_store` | Just before the pooled value is destroyed. Not called when a component is migrated between stores.                                        |

Registrations made in `on_attach` must key off stable state — the node's
transform or a heap object the component owns — never the component's own
address, because the pool may relocate it and a cross-scene re-parent moves it
to another pool altogether.

### No structural mutation during a traversal

`context::update` walks the tree and dispatches `on_update`; `set_active` (and
`add` / `remove`) dispatch `on_active_changed`. While such a walk is on the
stack (`context::is_traversing()`), the component and child lists being
iterated must not change, so the immediate node APIs — `add`, `remove`,
`set_active`, `add_component`, `remove_component`, `remove_all_components` —
detect the situation through the node's scene: in **debug builds they
assert**; in **release builds they log an error and apply the call at the end
of the current `context::update`** instead. Destroying a node from inside a
hook cannot be deferred by the node itself and is only reported.

The supported way for a hook to change the tree is to say so explicitly
through the scene's deferred command queue, reached via `node::scene()`:

```cpp
void on_update(runtime::node& owner)
{
    if (dead)
    {
        // Detach, free every component in the subtree, then let the owner
        // release the memory — all after the traversal has unwound.
        owner.scene()->defer_destroy(owner, [&owner] { release_node(&owner); });
    }
}
```

| Command                                   | Applied as                                                                                            |
| ----------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `defer_destroy(node&, release = {})`      | Detach from the parent, `remove_all_components` on the node and its descendants, then call `release`. |
| `defer_remove_component<C>(node&)`        | `node.remove_component<C>()`                                                                          |
| `defer_reparent(node&, node* new_parent)` | `new_parent->add(node)`, or detach to world space when `new_parent` is null.                          |
| `defer_set_active(node&, bool)`           | `node.set_active(active)`                                                                             |
| `defer(std::function<void()>)`            | The raw primitive the others are built on.                                                            |

Commands run in the order queued at the end of `context::update`, after the
walk (`apply_deferred()` runs them on demand outside a traversal). A command
may queue further commands; they run in the same drain. A node handed to
`defer_destroy` reports `is_destroy_pending()` until its command has run, and
a second `defer_destroy` for it is ignored.

**Ownership caveat.** Nodes are still caller-owned (the scene holds non-owning
links), so `defer_destroy` cannot free memory. It removes the node from the
scene in every way that matters — unlinks it, unwinds every renderer / light /
camera registration below it — and then hands control to the `release`
callback, never touching the node afterwards, so deleting it there is safe.
The subtree links are left intact for the owner's benefit. When the scene
takes ownership of nodes, `release` becomes the scene's own erase and the API
stays as it is.

### Re-parenting across scenes

Every node in a tree draws its components from the tree's store. `add(child)`
hands the parent's store down the child's subtree; a child that already holds
components in a *different* store has them **migrated** — moved value-by-value
into the new store's pools, keeping their external registrations, with no
`on_destroy` / `on_attach` round trip. A parent with no store leaves the
child's store alone. `set_store(nullptr)` unscopes a subtree; components
cannot live without a pool, so any it carries are freed (with `on_destroy`)
first.

## Status / roadmap

The model lands in stages:

1. **Foundation (done).** `core::pool` + handle; `node` as an entity
   with a type-erased `component_store`; transform-hierarchy world-matrix
   propagation.
2. **Component lifecycle hooks (done).** A component may define `on_attach(node&)`
   (called when added to a node) and/or `on_destroy()` (called just before its
   pool slot is freed); the store detects them with `if constexpr (requires ...)`,
   so plain-data components need neither. This is how a component bridges to a
   subsystem without the core knowing subsystem types. See *Lifecycle* above
   for the full hook table.
3. **`mesh_component` (done).** The first render component: owns a
   `rendering_engine::model` on the heap, registers it with the scene renderer in
   `on_attach`, parents it under the node so it draws at the node's world
   transform, and unregisters in `on_destroy`. Because the renderer keeps a
   non-owning pointer, the model lives behind a `unique_ptr` so its address — and
   thus the registration — survives the component being relocated in its pool.
4. **`light_component` / `camera_component` (done).** Same shape over the light
   and camera registries. Each owns its object behind a `unique_ptr` (the
   registries hold the object's address), and an `on_update(node&)` hook tracks
   the node: a point light's position and a directional light's direction follow
   the node's world transform, and a camera's position follows its node. Both
   honour `on_active_changed`: a disabled node's light leaves the registry
   (`light::set_enabled`) and its camera detaches, re-attaching on enable if
   no other camera has taken over.
5. **Traversal (done).** `runtime::context::update()` walks the tree from
   `root` once per frame — wired into `engine::tick` after game-module `on_frame`
   and before the draw — dispatching each component's `on_update` so node-derived
   state is refreshed against the settled world transforms, then applies the
   deferred commands the hooks queued. (A dirty-flag optimisation can replace
   the full walk later; correctness first.)
6. **Further subsystems** (audio, physics) adopt the same handle/pool shape as
   they appear.

The `external/scene_graph_demo_module` is the worked example: a solar system —
an emissive sun sphere with a shadow-casting point light (`light_component`) at
the centre, a handful of orbiting planet spheres (`mesh_component`), and
retrograde moons that orbit the other way — all driven purely by spinning the
orbit-pivot nodes, with scale confined to childless visual leaves so it never
contaminates a subtree. The sun's point light uses the omni shadow map (below),
so moons fall dark behind their planets and cast eclipse shadows.

## Point-light (omni) shadows

`point_shadow_pass` renders the scene's depth from the first shadow-casting
`point_light` into six perspective depth maps (±X/±Y/±Z, 90° FOV) — a cube map
emulated with six 2D targets, so the device needs no cube render-target support.
The `scene_pass` binds the six maps, their view-projections, and the light
position into the per-frame group; the lit shader selects the face by the major
axis of (fragment − light), projects, and PCF-compares to occlude that light.
Mirrors the directional `shadow_pass`. Set `point_light::cast_shadow` to enable;
only the first shadow-casting point light is honoured.
