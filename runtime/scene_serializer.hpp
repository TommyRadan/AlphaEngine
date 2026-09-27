/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file scene_serializer.hpp
 * @brief Saves a scene or a subtree to JSON and builds nodes back from it:
 *        scene files and prefabs.
 *
 * **Format.** A document is one JSON object:
 * @code
 * {
 *     "format": "alphaengine.scene",
 *     "version": 1,
 *     "nodes": [
 *         {
 *             "id": 1,
 *             "name": "sun",
 *             "parent": null,
 *             "active": true,
 *             "transform": {"position": [0, 0, 5], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1]},
 *             "components": [
 *                 {"type": "light", "fields": {"kind": "directional", "intensity": 1.0}},
 *                 {"type": "orbiting_sun", "fields": {"speed": 0.25}}
 *             ]
 *         }
 *     ]
 * }
 * @endcode
 * Nodes are listed depth first, every parent before its children and
 * siblings in order. @c id is unique within the file and @c parent names
 * the parent's id, or is @c null for a top-level node (a child of the scene
 * root, or of the node a prefab is instantiated under). @c active is the
 * node's own flag; the transform is local, rotation as a quaternion
 * (w, x, y, z). Each component entry names a type registered in
 * @ref runtime::default_type_registry and its fields, encoded by kind
 * (runtime/reflection.hpp): numbers, booleans and strings as themselves,
 * vectors / colours / quaternions as arrays, enumerations by name, objects
 * (a material) as @c {"type", "fields"}. A behaviour is an entry like any
 * other, named by its registered behaviour type. Asset references are VFS
 * paths or asset-cache keys resolved through the asset cache on load.
 *
 * **Placeholders.** A component that cannot be written as data — no
 * registered name, a renderable built in code, a mesh from a private upload
 * — is saved as @c {"type": ..., "placeholder": "<why>"} with a warning
 * rather than failing the save. A load keeps a placeholder, and any entry
 * it cannot rebuild (an unknown type, an asset that does not resolve), in a
 * @ref runtime::placeholder_component on the node, so saving the loaded
 * scene writes the same entries back: save, load and save again yield the
 * same document.
 *
 * All file I/O goes through the VFS (@c core::default_vfs). Loading and
 * instantiating build nodes with @c context::create_node and attach
 * components, so they must not run while the target scene is being updated.
 * Main-thread-only.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace runtime
{
    struct context;
    struct node;
    struct scene_manager;
    struct scene_io;

    /**
     * @brief A scene or prefab document in memory: parsed once, then
     *        instantiated as often as needed.
     *
     * Cheap to copy (the parsed document is shared and immutable). A
     * default-constructed document is valid and holds no nodes.
     */
    struct scene_document
    {
        /** @brief The empty document. */
        scene_document();

        /**
         * @brief Parses @p text; @c std::nullopt (with @p error set when
         *        non-null) when it is not JSON, not a scene document, or of a
         *        newer version than this build reads.
         */
        static std::optional<scene_document> parse(std::string_view text, std::string* error = nullptr);

        /** @brief Reads and parses the file @p path names through the VFS; see @ref parse. */
        static std::optional<scene_document> read(const std::filesystem::path& path, std::string* error = nullptr);

        /**
         * @brief The document as indented JSON text, ending in a newline.
         *
         * Deterministic: the same document always prints the same text, with
         * keys in the order the format lists them and short arrays of
         * numbers on one line.
         */
        std::string to_string() const;

        /** @brief Writes @ref to_string to @p path through the VFS; false (with @p error set) on failure. */
        bool write(const std::filesystem::path& path, std::string* error = nullptr) const;

        /** @brief Number of nodes in the document. */
        std::size_t node_count() const noexcept;

    private:
        friend struct scene_io;

        struct contents;
        explicit scene_document(std::shared_ptr<const contents> data);

        std::shared_ptr<const contents> m_data;
    };

    /** @brief A prefab is a scene document holding one subtree (see @ref save_subtree). */
    using prefab_document = scene_document;

    /**
     * @brief Saves @p root and everything below it as a document whose one
     *        top-level node is @p root (with its local pose) — a prefab.
     *
     * Nodes queued for destruction are left out. Problems (placeholders,
     * lossy fields) are logged as warnings; the save itself cannot fail.
     */
    prefab_document save_subtree(node& root);

    /** @brief Saves every node under @p scene's root (the root itself is implicit). */
    scene_document save_scene(context& scene);

    /** @brief @ref save_scene into the file @p path (through the VFS); false, with an error logged, on failure. */
    bool save_scene(context& scene, const std::filesystem::path& path);

    /**
     * @brief Builds @p document's nodes and components under @p parent and
     *        returns the nodes created at the top level.
     *
     * Each node is created with @c context::create_node in @p parent's
     * scene, posed, given its components — built by their registered
     * factories, their fields applied before they attach — and then its
     * active flag, so a disabled node's components start hidden. Problems
     * are logged as warnings and never stop the rest of the document: an
     * entry that cannot be rebuilt is kept as a placeholder. Nothing is
     * created, and an error is logged, when @p parent belongs to no scene or
     * its scene is mid-update. Instantiating a prefab is this call.
     */
    std::vector<node*> instantiate(const scene_document& document, node& parent);

    /**
     * @brief Loads the scene file @p path (through the VFS) as a scene of
     *        @p scenes and returns it, or @c nullptr (with an error logged).
     *
     * The scene is named after the file — its name up to the first dot, so
     * @c "levels/forest.scene.json" loads as @c "forest" — and loaded with
     * @c load_mode::additive when @p additive is set, @c load_mode::single
     * otherwise. A scene of that name that is already loaded is unloaded
     * first, so loading a file again replaces it. Not callable while the
     * scenes are updating.
     */
    context* load_scene(scene_manager& scenes, const std::filesystem::path& path, bool additive = false);

    /**
     * @brief Keeps @p resource alive until the @ref instantiate (or
     *        @ref load_scene) in progress returns; a no-op outside one.
     *
     * For asset resolvers: an imported model is held for the rest of the
     * load, so every mesh the document takes from it resolves from that one
     * import rather than importing the file again per mesh.
     */
    void keep_alive_while_loading(std::shared_ptr<const void> resource);
} // namespace runtime
