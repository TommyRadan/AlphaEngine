// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/reflection.hpp>

#include <core/log.hpp>

const char* runtime::field_kind_name(field_kind kind) noexcept
{
    switch (kind)
    {
    case field_kind::boolean:
        return "boolean";
    case field_kind::integer:
        return "integer";
    case field_kind::number:
        return "number";
    case field_kind::vec2:
        return "vec2";
    case field_kind::vec3:
        return "vec3";
    case field_kind::vec4:
        return "vec4";
    case field_kind::quat:
        return "quat";
    case field_kind::color:
        return "color";
    case field_kind::string:
        return "string";
    case field_kind::string_id:
        return "string_id";
    case field_kind::enumeration:
        return "enumeration";
    case field_kind::asset_ref:
        return "asset_ref";
    case field_kind::object:
        return "object";
    }
    return "unknown";
}

const runtime::field_value* runtime::find_field(const object_value& object, std::string_view name) noexcept
{
    for (const field_entry& entry : object.fields)
    {
        if (entry.name == name)
        {
            return &entry.value;
        }
    }
    return nullptr;
}

const runtime::enum_entry* runtime::field_info::find_enumerator(std::string_view enumerator_name) const noexcept
{
    for (const enum_entry& entry : enumerators)
    {
        if (entry.name == enumerator_name)
        {
            return &entry;
        }
    }
    return nullptr;
}

const runtime::enum_entry* runtime::field_info::find_enumerator(int64_t value) const noexcept
{
    for (const enum_entry& entry : enumerators)
    {
        if (entry.value == value)
        {
            return &entry;
        }
    }
    return nullptr;
}

const runtime::field_info* runtime::type_info::find_field(std::string_view field_name) const noexcept
{
    for (const field_info& field : fields)
    {
        if (field.name == field_name)
        {
            return &field;
        }
    }
    return nullptr;
}

std::vector<runtime::field_info> runtime::type_info::extra_fields(const void* object) const
{
    if (!instance_fields)
    {
        return {};
    }
    std::vector<field_info> extra = instance_fields(object);
    std::erase_if(extra, [this](const field_info& field) { return find_field(field.name) != nullptr; });
    return extra;
}

runtime::object_value runtime::type_info::snapshot(const void* object) const
{
    object_value value;
    value.type = name;
    auto read = [&](const field_info& field)
    {
        if (field.applies_to(object))
        {
            value.fields.push_back(field_entry{field.name, field.get(object)});
        }
    };
    for (const field_info& field : fields)
    {
        read(field);
    }
    for (const field_info& field : extra_fields(object))
    {
        read(field);
    }
    return value;
}

bool runtime::type_info::apply(void* object, const object_value& value) const
{
    bool applied = true;
    auto write = [&](const field_info& field)
    {
        const field_value* incoming = runtime::find_field(value, field.name);
        if (incoming == nullptr || !field.applies_to(object))
        {
            return;
        }
        if (!field.set(object, *incoming))
        {
            applied = false;
        }
    };
    for (const field_info& field : fields)
    {
        write(field);
    }
    // Asked only now: the type's own fields may have decided what they are.
    for (const field_info& field : extra_fields(object))
    {
        write(field);
    }
    return applied;
}

const runtime::type_info* runtime::type_registry::find(std::string_view name) const noexcept
{
    auto it = m_by_name.find(name);
    return it != m_by_name.end() ? it->second : nullptr;
}

const runtime::type_info* runtime::type_registry::find(std::type_index type) const noexcept
{
    auto it = m_by_type.find(type);
    return it != m_by_type.end() ? it->second : nullptr;
}

std::vector<const runtime::type_info*> runtime::type_registry::types() const
{
    std::vector<const type_info*> all;
    all.reserve(m_types.size());
    for (const std::unique_ptr<type_info>& info : m_types)
    {
        all.push_back(info.get());
    }
    return all;
}

runtime::type_info& runtime::type_registry::add(std::string name, type_category category, std::type_index type)
{
    auto created = std::make_unique<type_info>();
    created->name = std::move(name);
    created->category = category;
    created->type = type;

    const bool name_empty = created->name.empty();
    const bool name_taken = !name_empty && m_by_name.contains(created->name);
    // Objects may share a C++ type (two material templates over one data
    // struct); components and behaviours are also found by type, so one of
    // those C++ types carries exactly one name.
    const bool type_taken = category != type_category::object && m_by_type.contains(type);
    if (name_empty || name_taken || type_taken)
    {
        const char* reason = name_empty   ? "the name is empty"
                             : name_taken ? "the name is already registered"
                                          : "the type is already registered under another name";
        LOG_ERR("runtime::type_registry: cannot register '%s': %s; ignored", created->name.c_str(), reason);
        m_rejected.push_back(std::move(created));
        return *m_rejected.back();
    }

    type_info& added = *created;
    m_by_name.emplace(added.name, &added);
    if (category != type_category::object)
    {
        m_by_type.emplace(type, &added);
    }
    m_types.push_back(std::move(created));
    return added;
}

runtime::type_registry& runtime::default_type_registry()
{
    static type_registry instance;
    return instance;
}

bool runtime::add_type_registration(type_registration registration)
{
    if (registration != nullptr)
    {
        registration(default_type_registry());
    }
    return true;
}
