// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file reflection.hpp
 * @brief Registration by stable name of the component, behaviour and data
 *        types a scene is made of, with the fields that describe them.
 *
 * The component store keys its pools by @c std::type_index, which is not
 * stable across builds or compilers, so anything that has to name a type
 * outside the running process — a scene file, a prefab, an inspector
 * panel — goes through this registry instead. Each registered type has a
 * stable name, a factory and an ordered list of fields; each field has a
 * name, a @ref field_kind and a getter / setter pair over a type-erased
 * object, so a tool can read and write it without knowing the C++ type.
 *
 * Registration is explicit per type. A translation unit opens a block with
 * @ref REFLECT_TYPES and registers what it owns through the builder:
 * @code
 * REFLECT_TYPES()
 * {
 *     registry.register_component<light_component>("light")
 *         .field("intensity", get_intensity, set_intensity)
 *         .field("color", get_color, set_color).color();
 *     registry.register_behavior<orbiting_sun>("orbiting_sun");
 * }
 * @endcode
 * A type that defines @c static @c void @c reflect(type_builder<T>&) has it
 * called at registration, which is how a behaviour marks its tuned fields
 * from inside the class (private members included):
 * @code
 * static void reflect(runtime::type_builder<orbiting_sun>& type)
 * {
 *     type.field("speed", &orbiting_sun::m_speed);
 * }
 * @endcode
 *
 * Three categories share the registry and the field model:
 * - **components** — a type the node stores through @c add_component; the
 *   entry can find it on a node and attach a configured one;
 * - **behaviours** — a @ref runtime::behavior subclass, attached through a
 *   @c behavior_component and looked up by its dynamic type;
 * - **objects** — plain data that a field of another type holds by value
 *   (@ref field_kind::object), such as a material description.
 *
 * Registration runs at static-initialisation time and everything else on
 * the main thread; the registry is not synchronised.
 */

#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <core/math/quat.hpp>
#include <core/math/vec2.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>
#include <core/string_id.hpp>
#include <runtime/behavior.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    /** @brief What a field holds, and how a file or a tool presents it. */
    enum class field_kind : uint8_t
    {
        boolean,     /**< @c bool. */
        integer,     /**< Any integral type, carried as @c int64_t. */
        number,      /**< @c float or @c double, carried as @c float. */
        vec2,        /**< @c core::math::vec2. */
        vec3,        /**< @c core::math::vec3. */
        vec4,        /**< @c core::math::vec4. */
        quat,        /**< @c core::math::quat, written as (w, x, y, z). */
        color,       /**< A @c vec3 (RGB) or @c vec4 (RGBA) colour, linear floats. */
        string,      /**< @c std::string. */
        string_id,   /**< @c core::string_id, carried as its text. */
        enumeration, /**< An enum, carried as @c int64_t and written by the names in @ref field_info::enumerators. */
        asset_ref, /**< A string naming an asset: a VFS path or an asset-cache key (see @ref field_info::asset_type). */
        object,    /**< A registered object type held by value (see @ref object_value). */
    };

    /** @brief The lower-case name of @p kind, for logs and tools. */
    const char* field_kind_name(field_kind kind) noexcept;

    struct field_entry;

    /**
     * @brief A snapshot of a registered object type: its name and the value
     *        of each of its fields, in the type's field order.
     *
     * What a @ref field_kind::object field reads and writes. An empty
     * @ref type means "none" (a component with no material, say).
     */
    struct object_value
    {
        std::string type;
        std::vector<field_entry> fields;

        bool empty() const noexcept
        {
            return type.empty();
        }
    };

    /**
     * @brief The value of one field, in the representation every field of a
     *        kind shares.
     *
     * @c bool for @ref field_kind::boolean; @c int64_t for integer and
     * enumeration fields; @c float for numbers; the math types for vectors,
     * quaternions and colours; @c std::string for strings, string ids and
     * asset references; @ref object_value for objects.
     */
    using field_value = std::variant<bool,
                                     int64_t,
                                     float,
                                     core::math::vec2,
                                     core::math::vec3,
                                     core::math::vec4,
                                     core::math::quat,
                                     std::string,
                                     object_value>;

    /** @brief One named value of an @ref object_value. */
    struct field_entry
    {
        std::string name;
        field_value value;
    };

    /** @brief The value of the field named @p name in @p object, or @c nullptr. */
    const field_value* find_field(const object_value& object, std::string_view name) noexcept;

    /** @brief One named value of an enumeration field. */
    struct enum_entry
    {
        std::string name;
        int64_t value{0};
    };

    /**
     * @brief One field of a registered type: its name, kind and accessors.
     *
     * The accessors take the object type-erased — a pointer to the
     * component, the behaviour (its most-derived object) or the data object
     * — and convert through @ref field_value, so a caller never names the
     * C++ type. They may only be called on an object the field
     * @ref applies_to.
     */
    struct field_info
    {
        std::string name;
        field_kind kind{field_kind::number};

        /** @brief Reads the field of @p object. */
        std::function<field_value(const void* object)> get;

        /**
         * @brief Writes @p value into the field of @p object; false when the
         *        value has the wrong type or cannot be applied (an asset that
         *        does not resolve, say).
         */
        std::function<bool(void* object, const field_value& value)> set;

        /**
         * @brief Whether the field means anything for @p object; null for a
         *        field that always does. A light's range only applies to a
         *        point or spot light, say: a field that does not apply is
         *        neither saved nor shown.
         */
        std::function<bool(const void* object)> applies;

        /** @brief Enumeration fields: the named values. */
        std::vector<enum_entry> enumerators;

        /** @brief Asset-reference fields: what the reference names (@c "mesh", @c "texture", ...). */
        std::string asset_type;

        /**
         * @brief Whether the field may only be set before the component is
         *        attached: its setter rebuilds what the component owns (a
         *        camera of another projection, a mesh from another source),
         *        which an attached component has already registered with the
         *        renderer. A tool shows such a field read-only on a live
         *        component; a scene load sets it before attaching.
         */
        bool construct_only{false};

        /** @brief @ref applies, or true when the field has no predicate. */
        bool applies_to(const void* object) const
        {
            return !applies || applies(object);
        }

        /** @brief The enumerator named @p name, or @c nullptr. */
        const enum_entry* find_enumerator(std::string_view name) const noexcept;

        /** @brief The enumerator with value @p value, or @c nullptr. */
        const enum_entry* find_enumerator(int64_t value) const noexcept;
    };

    /** @brief Which kind of thing a registered type is; see the file notes. */
    enum class type_category : uint8_t
    {
        component,
        behavior,
        object,
    };

    /** @brief Everything the registry knows about one type. */
    struct type_info
    {
        /** @brief The stable name files and tools refer to the type by. */
        std::string name;
        type_category category{type_category::component};
        /** @brief The C++ type; for lookups inside the running process only. */
        std::type_index type{typeid(void)};
        /** @brief The fields, in registration order (also the order a load applies them in). */
        std::vector<field_info> fields;

        /**
         * @brief The fields one object carries beyond the type's own — a
         *        scripted behaviour's declared properties, say — or null for
         *        a type whose fields are all in @ref fields.
         *
         * Asked after the type's own fields have been applied, since they can
         * decide what the object carries (a script path names the script
         * that declares the properties). A save writes them after the type's
         * fields and a load applies the ones it finds; one named like a type
         * field is ignored. Their accessors take the same object pointer.
         */
        std::function<std::vector<field_info>(const void* object)> instance_fields;

        /** @brief Components: this type's component on @p owner, or @c nullptr. */
        std::function<void*(node& owner)> find;

        /**
         * @brief Components: builds a component from the type's factory, hands
         *        it — not yet attached — to @p configure, and attaches it to
         *        @p owner unless @p configure returns false. True when the
         *        component was attached.
         */
        std::function<bool(node& owner, const std::function<bool(void* component)>& configure)> attach;

        /** @brief Behaviours: a new instance from the type's factory. */
        std::function<std::unique_ptr<behavior>()> create_behavior;

        /** @brief Objects: a new default instance. */
        std::function<std::shared_ptr<void>()> create;

        /**
         * @brief Why @p object cannot be written as data — a mesh built from a
         *        private in-memory upload, say — or an empty string when it
         *        can. Null for a type that can always be written. A scene
         *        save writes a placeholder for such an object instead.
         */
        std::function<std::string(const void* object)> placeholder_reason;

        /**
         * @brief What a save of @p object cannot capture exactly, or an empty
         *        string when it captures everything. Null for a type whose
         *        fields are the whole story. A scene save logs it as a
         *        warning and writes the fields anyway.
         */
        std::function<std::string(const void* object)> lossy_reason;

        /** @brief The field named @p field_name, or @c nullptr. */
        const field_info* find_field(std::string_view field_name) const noexcept;

        /**
         * @brief @ref instance_fields of @p object, less any named like one of
         *        the type's own fields; empty for a type without them.
         */
        std::vector<field_info> extra_fields(const void* object) const;

        /**
         * @brief Reads every field that applies to @p object, its
         *        @ref extra_fields included, into an @ref object_value named
         *        after this type.
         */
        object_value snapshot(const void* object) const;

        /**
         * @brief Writes the fields @p value carries into @p object, in this
         *        type's field order and then its @ref extra_fields, skipping
         *        the ones @p object does not have or that do not apply. False
         *        when a setter refused.
         */
        bool apply(void* object, const object_value& value) const;
    };

    namespace reflection_detail
    {
        template<typename V>
        inline constexpr bool unsupported_field_type = false;

        // The kind a field of C++ type V gets unless the builder says
        // otherwise (color and asset_ref are always chosen explicitly).
        template<typename V>
        constexpr field_kind kind_of() noexcept
        {
            if constexpr (std::is_same_v<V, bool>)
            {
                return field_kind::boolean;
            }
            else if constexpr (std::is_enum_v<V>)
            {
                return field_kind::enumeration;
            }
            else if constexpr (std::is_integral_v<V>)
            {
                return field_kind::integer;
            }
            else if constexpr (std::is_floating_point_v<V>)
            {
                return field_kind::number;
            }
            else if constexpr (std::is_same_v<V, core::math::vec2>)
            {
                return field_kind::vec2;
            }
            else if constexpr (std::is_same_v<V, core::math::vec3>)
            {
                return field_kind::vec3;
            }
            else if constexpr (std::is_same_v<V, core::math::vec4>)
            {
                return field_kind::vec4;
            }
            else if constexpr (std::is_same_v<V, core::math::quat>)
            {
                return field_kind::quat;
            }
            else if constexpr (std::is_same_v<V, std::string>)
            {
                return field_kind::string;
            }
            else if constexpr (std::is_same_v<V, core::string_id>)
            {
                return field_kind::string_id;
            }
            else if constexpr (std::is_same_v<V, object_value>)
            {
                return field_kind::object;
            }
            else
            {
                static_assert(unsupported_field_type<V>, "runtime::type_builder: unsupported field type");
                return field_kind::number;
            }
        }

        template<typename V>
        field_value to_value(const V& value)
        {
            if constexpr (std::is_same_v<V, bool>)
            {
                return field_value{std::in_place_type<bool>, value};
            }
            else if constexpr (std::is_enum_v<V> || std::is_integral_v<V>)
            {
                return field_value{std::in_place_type<int64_t>, static_cast<int64_t>(value)};
            }
            else if constexpr (std::is_floating_point_v<V>)
            {
                return field_value{std::in_place_type<float>, static_cast<float>(value)};
            }
            else if constexpr (std::is_same_v<V, core::string_id>)
            {
                return field_value{std::in_place_type<std::string>, std::string{value.view()}};
            }
            else
            {
                return field_value{std::in_place_type<V>, value};
            }
        }

        template<typename V>
        bool from_value(const field_value& value, V& out)
        {
            if constexpr (std::is_same_v<V, bool>)
            {
                if (const bool* flag = std::get_if<bool>(&value))
                {
                    out = *flag;
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_enum_v<V>)
            {
                if (const int64_t* number = std::get_if<int64_t>(&value))
                {
                    out = static_cast<V>(*number);
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_integral_v<V>)
            {
                const int64_t* number = std::get_if<int64_t>(&value);
                if (number == nullptr || !std::in_range<V>(*number))
                {
                    return false;
                }
                out = static_cast<V>(*number);
                return true;
            }
            else if constexpr (std::is_floating_point_v<V>)
            {
                if (const float* number = std::get_if<float>(&value))
                {
                    out = static_cast<V>(*number);
                    return true;
                }
                if (const int64_t* number = std::get_if<int64_t>(&value))
                {
                    out = static_cast<V>(*number);
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_same_v<V, core::math::vec3>)
            {
                // A colour may arrive with or without its alpha.
                if (const auto* xyz = std::get_if<core::math::vec3>(&value))
                {
                    out = *xyz;
                    return true;
                }
                if (const auto* xyzw = std::get_if<core::math::vec4>(&value))
                {
                    out = core::math::vec3{xyzw->x, xyzw->y, xyzw->z};
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_same_v<V, core::math::vec4>)
            {
                if (const auto* xyzw = std::get_if<core::math::vec4>(&value))
                {
                    out = *xyzw;
                    return true;
                }
                if (const auto* xyz = std::get_if<core::math::vec3>(&value))
                {
                    out = core::math::vec4{*xyz, 1.0f};
                    return true;
                }
                return false;
            }
            else if constexpr (std::is_same_v<V, core::string_id>)
            {
                if (const std::string* text = std::get_if<std::string>(&value))
                {
                    out = core::string_id{*text};
                    return true;
                }
                return false;
            }
            else
            {
                if (const V* same = std::get_if<V>(&value))
                {
                    out = *same;
                    return true;
                }
                return false;
            }
        }
    } // namespace reflection_detail

    /**
     * @brief Fluent registration of one type's fields; handed out by the
     *        @ref type_registry register_* calls and to a type's own
     *        @c reflect function.
     *
     * The modifiers (@ref color, @ref asset, @ref enumerators,
     * @ref construct_only, @ref when) apply to the field added last.
     */
    template<typename T>
    struct type_builder
    {
        explicit type_builder(type_info& info) noexcept : m_info{&info} {}

        /** @brief A field over the data member @p member (of @c T or a base of it). */
        template<typename V, typename Owner>
        type_builder& field(std::string name, V Owner::*member)
        {
            static_assert(std::is_base_of_v<Owner, T>, "runtime::type_builder::field: not a member of this type");
            static_assert(!std::is_function_v<V>, "runtime::type_builder::field: pass accessors for a member function");
            field_info& added = add(std::move(name), reflection_detail::kind_of<V>());
            added.get = [member](const void* object)
            { return reflection_detail::to_value<V>(static_cast<const T*>(object)->*member); };
            added.set = [member](void* object, const field_value& value)
            { return reflection_detail::from_value<V>(value, static_cast<T*>(object)->*member); };
            return *this;
        }

        /**
         * @brief A field read by @p get — @c V(const T&) — and written by
         *        @p set — @c void(T&, V) or @c bool(T&, V), false refusing
         *        the value. Either may be a member function pointer
         *        (@c &rigidbody_component::mass,
         *        @c &rigidbody_component::set_mass).
         */
        template<typename Get, typename Set>
            requires std::invocable<Get&, const T&>
        type_builder& field(std::string name, Get get, Set set)
        {
            using value_type = std::remove_cvref_t<std::invoke_result_t<Get&, const T&>>;
            field_info& added = add(std::move(name), reflection_detail::kind_of<value_type>());
            added.get = [get](const void* object)
            { return reflection_detail::to_value<value_type>(std::invoke(get, *static_cast<const T*>(object))); };
            added.set = [set](void* object, const field_value& value)
            {
                value_type converted{};
                if (!reflection_detail::from_value<value_type>(value, converted))
                {
                    return false;
                }
                T& target = *static_cast<T*>(object);
                if constexpr (std::is_same_v<std::invoke_result_t<Set&, T&, value_type>, bool>)
                {
                    return std::invoke(set, target, std::move(converted));
                }
                else
                {
                    std::invoke(set, target, std::move(converted));
                    return true;
                }
            };
            return *this;
        }

        /** @brief Marks the last field (a @c vec3 or @c vec4) as a colour. */
        type_builder& color()
        {
            field_info& target = last();
            assert((target.kind == field_kind::vec3 || target.kind == field_kind::vec4) &&
                   "runtime::type_builder::color: the field is not a vec3 / vec4");
            target.kind = field_kind::color;
            return *this;
        }

        /** @brief Marks the last field (a @c std::string) as a reference to an asset of @p asset_type. */
        type_builder& asset(std::string asset_type)
        {
            field_info& target = last();
            assert(target.kind == field_kind::string && "runtime::type_builder::asset: the field is not a string");
            target.kind = field_kind::asset_ref;
            target.asset_type = std::move(asset_type);
            return *this;
        }

        /** @brief Names the values of the last field (an enum). */
        type_builder& enumerators(std::vector<enum_entry> values)
        {
            field_info& target = last();
            assert(target.kind == field_kind::enumeration && "runtime::type_builder::enumerators: not an enum field");
            target.enumerators = std::move(values);
            return *this;
        }

        /** @brief Marks the last field as settable only before attachment (see @ref field_info::construct_only). */
        type_builder& construct_only()
        {
            last().construct_only = true;
            return *this;
        }

        /** @brief Makes the last field apply only where @p predicate — @c bool(const T&) — holds. */
        template<typename Predicate>
        type_builder& when(Predicate predicate)
        {
            last().applies = [predicate](const void* object) { return predicate(*static_cast<const T*>(object)); };
            return *this;
        }

        /** @brief Sets the type's @ref type_info::placeholder_reason from @p reason — @c std::string(const T&). */
        template<typename Reason>
        type_builder& placeholder_reason(Reason reason)
        {
            m_info->placeholder_reason = [reason](const void* object) -> std::string
            { return reason(*static_cast<const T*>(object)); };
            return *this;
        }

        /**
         * @brief Sets the type's @ref type_info::instance_fields from
         *        @p fields — @c std::vector<field_info>(const T&).
         */
        template<typename Fields>
        type_builder& instance_fields(Fields fields)
        {
            m_info->instance_fields = [fields](const void* object) -> std::vector<field_info>
            { return fields(*static_cast<const T*>(object)); };
            return *this;
        }

        /** @brief Sets the type's @ref type_info::lossy_reason from @p reason — @c std::string(const T&). */
        template<typename Reason>
        type_builder& lossy_reason(Reason reason)
        {
            m_info->lossy_reason = [reason](const void* object) -> std::string
            { return reason(*static_cast<const T*>(object)); };
            return *this;
        }

        /** @brief The entry being built. */
        const type_info& info() const noexcept
        {
            return *m_info;
        }

    private:
        field_info& add(std::string name, field_kind kind)
        {
            field_info added;
            added.name = std::move(name);
            added.kind = kind;
            m_info->fields.push_back(std::move(added));
            return m_info->fields.back();
        }

        field_info& last()
        {
            assert(!m_info->fields.empty() && "runtime::type_builder: no field to modify yet");
            return m_info->fields.back();
        }

        type_info* m_info;
    };

    /**
     * @brief Every registered type, by stable name (and, for components and
     *        behaviours, by C++ type within this process).
     *
     * The process-wide instance is @ref default_type_registry. A second
     * registration under a name already taken is rejected with an error and
     * leaves the first in place.
     */
    struct type_registry
    {
        type_registry() = default;
        type_registry(const type_registry&) = delete;
        type_registry& operator=(const type_registry&) = delete;

        /** @brief Registers component @c C, built by value-initialisation. */
        template<typename C>
        type_builder<C> register_component(std::string name)
        {
            return register_component<C>(std::move(name), [] { return C{}; });
        }

        /** @brief Registers component @c C, built by @p factory — @c C(). */
        template<typename C, typename Factory>
        type_builder<C> register_component(std::string name, Factory factory)
        {
            type_info& info = add(std::move(name), type_category::component, std::type_index{typeid(C)});
            info.find = [](node& owner) -> void* { return owner.get_component<C>(); };
            info.attach = [factory](node& owner, const std::function<bool(void*)>& configure)
            {
                C value = factory();
                if (configure && !configure(&value))
                {
                    return false;
                }
                return owner.add_component<C>(std::move(value)) != nullptr;
            };
            return finish<C>(info);
        }

        /** @brief Registers behaviour @c B, built by its default constructor. */
        template<typename B>
        type_builder<B> register_behavior(std::string name)
        {
            return register_behavior<B>(std::move(name), [] { return std::make_unique<B>(); });
        }

        /** @brief Registers behaviour @c B, built by @p factory — @c std::unique_ptr<B>(). */
        template<typename B, typename Factory>
        type_builder<B> register_behavior(std::string name, Factory factory)
        {
            static_assert(std::is_base_of_v<behavior, B>,
                          "runtime::type_registry: B must derive from runtime::behavior");
            type_info& info = add(std::move(name), type_category::behavior, std::type_index{typeid(B)});
            info.create_behavior = [factory]() -> std::unique_ptr<behavior> { return factory(); };
            return finish<B>(info);
        }

        /** @brief Registers the data type @c T as an object type, built by value-initialisation. */
        template<typename T>
        type_builder<T> register_object(std::string name)
        {
            type_info& info = add(std::move(name), type_category::object, std::type_index{typeid(T)});
            info.create = []() -> std::shared_ptr<void> { return std::make_shared<T>(); };
            return finish<T>(info);
        }

        /** @brief The type registered as @p name, or @c nullptr. */
        const type_info* find(std::string_view name) const noexcept;

        /** @brief The component or behaviour type registered for @p type, or @c nullptr. */
        const type_info* find(std::type_index type) const noexcept;

        /** @brief Every registered type, in registration order. */
        std::vector<const type_info*> types() const;

    private:
        type_info& add(std::string name, type_category category, std::type_index type);

        template<typename T>
        type_builder<T> finish(type_info& info)
        {
            type_builder<T> builder{info};
            if constexpr (requires(type_builder<T>& b) { T::reflect(b); })
            {
                T::reflect(builder);
            }
            return builder;
        }

        std::vector<std::unique_ptr<type_info>> m_types;
        std::map<std::string, type_info*, std::less<>> m_by_name;
        std::unordered_map<std::type_index, type_info*> m_by_type;
        // Entries whose name was already taken: kept alive so their builder
        // has somewhere to write, but never found.
        std::vector<std::unique_ptr<type_info>> m_rejected;
    };

    /** @brief The process-wide registry that scene files and tools read. */
    type_registry& default_type_registry();

    /** @brief A registration function, as @ref REFLECT_TYPES declares one. */
    using type_registration = void (*)(type_registry& registry);

    /** @brief Runs @p registration against @ref default_type_registry; returns true (for a static initialiser). */
    bool add_type_registration(type_registration registration);
} // namespace runtime

/**
 * @brief Opens the registration block of a translation unit: the body that
 *        follows runs once, at static-initialisation time, with
 *        @c registry naming the @ref runtime::default_type_registry. One per
 *        translation unit.
 */
#define REFLECT_TYPES()                                                                                                \
    static void reflect_types(runtime::type_registry& registry);                                                       \
    [[maybe_unused]] static const bool reflect_types_registered = runtime::add_type_registration(&reflect_types);      \
    static void reflect_types([[maybe_unused]] runtime::type_registry& registry)
