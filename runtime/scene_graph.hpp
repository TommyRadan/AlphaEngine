/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
 * @file scene_graph.hpp
 * @brief Scene graph subsystem entry point.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/string_id.hpp>
#include <runtime/component.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    /**
     * @brief A scene: owns its nodes, the component store they draw from and
     *        the @ref root of their hierarchy.
     *
     * Owned by @ref runtime::scene_manager (one persistent scene plus whatever
     * is loaded). Brings itself up and down with the same @ref init / @ref quit
     * shape as the other subsystems.
     *
     * **Nodes.** @ref create_node allocates a node in the scene's node pool —
     * fixed-size pages, so its address is stable for its whole life — names
     * it, and links it under a parent (the root by default). @ref destroy_node
     * retires a node and everything below it at the end of the update. What
     * is still alive when the scene quits is freed then, while the renderer
     * the components unwind against is still up, so no node outlives the
     * engine. A node constructed directly (a stack node, a test fixture) can
     * still be linked in; it stays owned by its caller.
     *
     * **Update.** @ref update walks the tree once, depth-first, to settle every
     * world matrix and stamp each effectively active node with its place in
     * the walk, then runs @c on_update one component type at a time over that
     * type's pool (see @ref component_store::update_visited): parents before
     * children within a type, types in the order they first appeared in the
     * scene. @ref each and @ref view expose the same per-type pools to
     * systems.
     *
     * **Deferred commands.** While @ref update (or @ref each, or a
     * @ref view) is iterating, the node, child and component lists must not
     * change. Any structural change a hook wants — destroy the node it runs
     * on, drop a component, move a node, disable a subtree — is queued with
     * @ref destroy_node, @ref defer_remove_component, @ref defer_reparent or
     * @ref defer_set_active (or the raw @ref defer) and applied, in the order
     * queued, once the walk has finished. Commands queued outside a traversal
     * are applied at the end of the next @ref update as well, or immediately
     * by @ref apply_deferred.
     */
    struct context
    {
        /**
         * @brief RAII marker for a traversal in progress.
         *
         * Constructed around every walk that dispatches component hooks or
         * iterates a component pool — @ref update, @ref each, a live
         * @ref view, @ref node::update_subtree and the active-state refresh —
         * so that while any such walk is on the stack @ref is_traversing is
         * true and the node's immediate structural APIs refuse (debug) or
         * defer (release). Nesting is fine; a null scene makes it a no-op.
         */
        struct traversal_scope
        {
            explicit traversal_scope(context* scene) noexcept;
            ~traversal_scope();

            traversal_scope(const traversal_scope&) = delete;
            traversal_scope& operator=(const traversal_scope&) = delete;

        private:
            context* m_scene;
        };

    private:
        // Declared before components and root so they are destroyed after
        // them: the root's destructor orphans its children, whose active-state
        // refresh may still mark a traversal or queue a command here, and
        // every node leaving the scene drops out of the name index.
        int m_traversal_depth{0};
        std::vector<std::function<void()>> m_pending;
        std::unordered_map<core::string_id, std::vector<node*>> m_name_index;
        // Scene-owned nodes whose destroy command has run; freed once the
        // drain that ran it has finished, so a later command in the same
        // drain that still names one touches live (if detached) memory.
        std::vector<node*> m_doomed;

    public:
        context();
        ~context();

        context(const context&) = delete;
        context& operator=(const context&) = delete;
        context(context&&) = delete;
        context& operator=(context&&) = delete;

        /** @brief Initializes the scene graph subsystem. */
        void init();

        /**
         * @brief Shuts the scene down: applies the queued commands, then
         *        frees every node the scene owns.
         *
         * Components are destroyed with their nodes (dispatching
         * @c on_destroy), so their renderer / light / camera registrations
         * unwind here, before the engine takes the renderer down. A
         * caller-owned subtree still linked in is cut loose the way the
         * destructor does it (components freed, store dropped, links below
         * kept). The scene is empty but usable afterwards.
         */
        void quit();

        /**
         * @brief Advances the scene one frame: settles the world transforms,
         *        dispatches @c on_update per component type, then applies
         *        every deferred command.
         *
         * Called once per rendered frame (through the scene manager) from
         * @ref runtime::engine::tick after the fixed steps and the
         * @c core::render_update listeners, and before the renderer draws.
         * Only nodes linked under @ref root and effectively active are
         * updated.
         */
        void update();

        // --- Scene-owned nodes ---------------------------------------------

        /**
         * @brief Creates a node owned by this scene, named @p name, under
         *        @p parent (the @ref root when null), and returns it.
         *
         * The node lives at a stable address until @ref destroy_node (or the
         * scene quits). It is scoped to this scene immediately, so it can
         * reach the deferred queue and @ref find sees it; if @p parent's scene
         * is mid-traversal the link itself is deferred to the end of that
         * update (components still cannot be added until then either — see
         * the class notes).
         */
        node& create_node(core::string_id name = {}, node* parent = nullptr);

        /**
         * @brief Destroys @p target and its subtree at the end of the current
         *        (or next) update.
         *
         * When the command runs the node is unlinked from its parent, every
         * component in the subtree is freed (dispatching @c on_destroy), and
         * every scene-owned node in the subtree is freed once the drain is
         * over. A descendant with its own pending destroy is left to it; a
         * caller-owned descendant is orphaned (it has already lost its
         * components). @p target reports @ref node::is_destroy_pending until
         * it is gone, and a second request for it is ignored. A node owned by
         * another scene is forwarded to that scene; a caller-owned node gets
         * @ref defer_destroy (the scene cannot free its memory). The root
         * cannot be destroyed.
         */
        void destroy_node(node& target);

        /**
         * @brief Deep-copies @p source and its subtree into this scene under
         *        @p parent (the @ref root when null) and returns the copy.
         *
         * Every copied node takes its source's name, local pose and active
         * flag, and a copy of each component whose type can be copied — one
         * defining @c C @c clone() @c const, or else a copy-constructible
         * one; a component of neither kind is skipped with a warning, as is
         * one whose @c clone() returns an empty @c std::optional<C>. Copies
         * attach (@c on_attach) as @c add_component would. The subtree is
         * planned before anything is created, so cloning a node under its
         * own descendant copies the original subtree once. Called while the
         * target scene is mid-traversal, the returned node exists (named and
         * posed) immediately and is populated at the end of that update.
         */
        node& clone(node& source, node* parent = nullptr);

        /** @brief Number of live nodes this scene owns (the root is not one). */
        std::size_t node_count() const noexcept;

        /**
         * @brief A node in this scene named @p name, or @c nullptr.
         *
         * An O(1) probe of the scene's name index, which holds every named
         * node scoped to this scene (linked under the root or not). When
         * several share the name, the one that has held it longest is
         * returned. The empty name is not indexed and finds nothing; for a
         * depth-first search of one subtree use @ref node::find.
         */
        node* find(core::string_id name);

        // --- Component queries ---------------------------------------------

        /**
         * @brief Invokes @p fn on every @c C in this scene, as one pass over
         *        @c C's pool.
         *
         * @p fn takes @c (node&, C&) or @c (C&). Every live @c C is visited,
         * active or not, linked under the root or not, in pool slot order
         * (neither creation nor hierarchy order); filter with
         * @ref node::is_effective_active where it matters. Marks a traversal
         * while it runs, so structural changes from @p fn defer (see the
         * class notes).
         */
        template<typename C, typename Fn>
        void each(Fn&& fn);

        /**
         * @brief A range over every node carrying all of @c C and @c Rest,
         *        yielding @c std::tuple<node&, C&, Rest&...>.
         *
         * @code
         * for (auto [owner, mesh, light] : scene.view<mesh_component, light_component>())
         * @endcode
         * Walks @c C's pool (so name the rarest type first) in slot order and
         * skips owners lacking any of @c Rest. The view marks a traversal of
         * this scene for as long as it lives; keep it to the loop.
         */
        template<typename C, typename... Rest>
        component_view<C, Rest...> view();

        // --- Deferred commands ---------------------------------------------

        /**
         * @brief Queues an arbitrary command for the end of the current (or
         *        next) @ref update.
         *
         * The typed helpers below are built on this. A command may queue
         * further commands; they run in the same drain.
         */
        void defer(std::function<void()> command);

        /**
         * @brief Queues the unlinking of @p target: detach it from its
         *        parent, free every component on it and its descendants
         *        (dispatching @c on_destroy), then invoke @p release.
         *
         * What @ref destroy_node does for a node the scene does not own:
         * @p release is where the owner frees the memory (e.g. erase the
         * @c unique_ptr holding the node). The node is not touched after
         * @p release runs, so deleting it there is safe. Without a release
         * callback the node survives as a detached, component-less subtree
         * the owner may reuse or delete later. @p target must stay alive until
         * the command has run — its @ref node::is_destroy_pending reports
         * @c true in the meantime — and a second call for a node already
         * pending is ignored.
         */
        void defer_destroy(node& target, std::function<void()> release = {});

        /** @brief Queues @c target.remove_component<C>() for the end of the update. */
        template<typename C>
        void defer_remove_component(node& target)
        {
            defer([&target] { target.remove_component<C>(); });
        }

        /**
         * @brief Queues a re-parent of @p target under @p new_parent, or a
         *        detach to world space when @p new_parent is @c nullptr.
         *
         * Applied through @ref node::add / @ref node::remove, so an ancestor
         * cycle is rejected (with an error logged) at that point.
         */
        void defer_reparent(node& target, node* new_parent);

        /** @brief Queues @c target.set_active(active) for the end of the update. */
        void defer_set_active(node& target, bool active);

        /**
         * @brief Applies every queued command now, in queue order, including
         *        any a command queues while running, then frees the nodes the
         *        destroy commands retired.
         *
         * Called by @ref update once the traversal has finished; callable
         * directly from outside a traversal (e.g. after a batch of explicit
         * deferrals). Logs an error and leaves the queue untouched if a
         * traversal is in progress.
         */
        void apply_deferred();

        /** @brief Number of commands waiting to be applied. */
        std::size_t pending_command_count() const noexcept;

        /** @brief True while a walk that dispatches component hooks or iterates a pool is on the stack. */
        bool is_traversing() const noexcept;

        /**
         * @brief Pools backing every node's components.
         *
         * One @ref core::pool per component type; nodes hold handles
         * into it rather than owning component data. Declared before @ref root
         * so it outlives the node tree and is still alive when nodes free their
         * components during teardown.
         */
        component_store components;

        /**
         * @brief Root of the scene hierarchy.
         *
         * Sits at world origin with identity transform and is wired to
         * @ref components, so nodes added under it (directly or transitively)
         * inherit the store and can carry components. It has no special
         * behaviour beyond being a conventional, always-present parent, and is
         * not one of the scene-owned nodes @ref create_node hands out.
         */
        node root;

    private:
        friend struct node;

        /**
         * @brief Storage for scene-owned nodes: fixed-size pages that never
         *        move, so a node keeps its address for its whole life.
         *
         * Freed slots are recycled; the pages themselves are kept until the
         * scene dies.
         */
        struct node_pool
        {
            node_pool() = default;
            node_pool(const node_pool&) = delete;
            node_pool& operator=(const node_pool&) = delete;

            /** @brief Constructs a node in a free slot and reports the slot. */
            node& allocate(uint32_t& slot);

            /** @brief Destroys the node in @p slot and frees the slot. */
            void release(uint32_t slot);

            /** @brief Every live node, in slot order. */
            std::vector<node*> live() const;

            std::size_t size() const noexcept
            {
                return m_live;
            }

        private:
            static constexpr std::size_t k_page_size = 64;

            struct page
            {
                std::array<std::optional<node>, k_page_size> slots;
            };

            std::optional<node>& cell(uint32_t slot) const noexcept
            {
                return m_pages[slot / k_page_size]->slots[slot % k_page_size];
            }

            std::vector<std::unique_ptr<page>> m_pages;
            std::vector<uint32_t> m_free;
            std::size_t m_live{0};
        };

        // Name index maintenance, driven by node on every change of name or
        // of scene.
        void index_name(node& target);
        void unindex_name(node& target);

        // The update walk: settles world matrices and stamps visit marks.
        void propagate(node& target, uint64_t stamp, uint32_t& order);

        // Unlinks @p target and frees the components of its subtree.
        void unlink_and_strip(node& target);

        // Queues the scene-owned nodes of @p target's subtree for freeing,
        // children before parents.
        void collect_doomed(node& target);

        // Frees what collect_doomed queued.
        void free_doomed();

        // Destroys a node this scene owns and recycles its slot.
        void free_node(node& target);

        // Teardown shared by quit and the destructor.
        void release_all();

        // Copies @p source's subtree (components, children, flags) onto the
        // fresh node @p copy.
        void clone_contents(node& source, node& copy);

        // Declared after root so it is destroyed first; empty by then, as
        // release_all has already freed every node in it.
        node_pool m_nodes;
    };

    /**
     * @brief Range over every node carrying all of @c C and @c Rest in a
     *        scene; see @ref context::view.
     *
     * Not copyable or movable (it holds the traversal marker); use it in
     * place, as a range-for subject or through @ref each.
     */
    template<typename C, typename... Rest>
    struct component_view
    {
        using value_type = std::tuple<node&, C&, Rest&...>;

        struct iterator
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = component_view::value_type;
            using reference = value_type;

            iterator() = default;

            value_type operator*() const
            {
                node& owner = *m_pool->owner_at(m_it.handle().index).owner;
                return value_type{owner, *m_it, *owner.get_component<Rest>()...};
            }

            iterator& operator++()
            {
                ++m_it;
                settle();
                return *this;
            }

            iterator operator++(int)
            {
                iterator copy = *this;
                ++*this;
                return copy;
            }

            bool operator==(const iterator& other) const noexcept
            {
                return m_it == other.m_it;
            }

            bool operator!=(const iterator& other) const noexcept
            {
                return !(*this == other);
            }

        private:
            friend struct component_view;

            using pool_type = component_store::typed_pool<C>;
            using pool_iterator = typename core::pool<C>::iterator;

            iterator(pool_type* pool, pool_iterator it, pool_iterator end) : m_pool{pool}, m_it{it}, m_end{end}
            {
                settle();
            }

            // Advances past components with no owner or whose owner lacks
            // one of the other types.
            void settle()
            {
                while (m_it != m_end && !matches())
                {
                    ++m_it;
                }
            }

            bool matches() const
            {
                const node* owner = m_pool->owner_at(m_it.handle().index).owner;
                return owner != nullptr && (owner->has_component<Rest>() && ...);
            }

            pool_type* m_pool{nullptr};
            pool_iterator m_it{};
            pool_iterator m_end{};
        };

        component_view(const component_view&) = delete;
        component_view& operator=(const component_view&) = delete;

        iterator begin()
        {
            return m_pool != nullptr ? iterator{m_pool, m_pool->data.begin(), m_pool->data.end()} : iterator{};
        }

        iterator end()
        {
            return m_pool != nullptr ? iterator{m_pool, m_pool->data.end(), m_pool->data.end()} : iterator{};
        }

        /** @brief Invokes @c fn(node&, C&, Rest&...) for every match. */
        template<typename Fn>
        void each(Fn&& fn)
        {
            for (value_type entry : *this)
            {
                std::apply(fn, entry);
            }
        }

    private:
        friend struct context;

        explicit component_view(context& scene) : m_scope{&scene}, m_pool{scene.components.find_pool<C>()} {}

        context::traversal_scope m_scope;
        component_store::typed_pool<C>* m_pool;
    };

    template<typename C, typename Fn>
    void context::each(Fn&& fn)
    {
        traversal_scope traversal{this};
        components.each<C>(std::forward<Fn>(fn));
    }

    template<typename C, typename... Rest>
    component_view<C, Rest...> context::view()
    {
        return component_view<C, Rest...>{*this};
    }
} // namespace runtime
