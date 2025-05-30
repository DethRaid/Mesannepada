#pragma once

#include <compare>
#include <typeindex>
#include <EASTL/vector.h>

#include <spdlog/spdlog.h>

template <typename ObjectType>
class ObjectPool;

template <typename ObjectType>
struct PooledObject {
    uint32_t index = 0xFFFFFFFF;

    ObjectPool<ObjectType>* pool = nullptr;

    ObjectType* operator->() const;

    ObjectType& operator*() const;

    explicit operator bool() const;

    bool operator!() const;

    bool operator==(const PooledObject& other) const;

    auto operator<=>(const PooledObject<ObjectType>& other) const;

    bool is_valid() const;
};

template <typename ObjectType>
class ObjectPool {
public:
    ObjectPool();

    template <typename CreateFunc, typename DestroyFunc>
    explicit ObjectPool(CreateFunc&& creator_in, DestroyFunc&& deleter_in);

    ~ObjectPool();

    PooledObject<ObjectType> emplace(ObjectType&& object);

    PooledObject<ObjectType> create_object();

    ObjectType& get_object(const PooledObject<ObjectType>& handle);

    ObjectType free_object(const PooledObject<ObjectType>& handle);

    ObjectType free_object(uint32_t index);

    PooledObject<ObjectType> make_handle(uint32_t index_in);

    eastl::vector<ObjectType>& get_data();

    const eastl::vector<ObjectType>& get_data() const;

    ObjectType& operator[](uint32_t index);

    const ObjectType& operator[](uint32_t index) const;

    uint32_t size() const;

private:
    uint32_t num_objects = 0;

    std::function<ObjectType()> creator;

    std::function<void(ObjectType&&)> deleter;

    eastl::vector<ObjectType> objects;

    eastl::vector<PooledObject<ObjectType>> available_handles;
};

template <typename ObjectType>
uint32_t ObjectPool<ObjectType>::size() const { return num_objects; }

template <typename ObjectType>
struct std::hash<PooledObject<ObjectType>> {
    size_t operator()(const PooledObject<ObjectType>& value) const noexcept {
        // This completely fails for some reason
        return std::hash<uint32_t>{}(value.index);
    }
};

template <typename ObjectType>
ObjectType* PooledObject<ObjectType>::operator->() const {
    auto& objects = pool->get_data();
    return &objects[index];
}

template <typename ObjectType>
ObjectType& PooledObject<ObjectType>::operator*() const {
    auto& objects = pool->get_data();
    return objects[index];
}

template <typename ObjectType>
PooledObject<ObjectType>::operator bool() const {
    return is_valid();
}

template <typename ObjectType>
bool PooledObject<ObjectType>::operator!() const {
    return !operator bool();
}

template <typename ObjectType>
bool PooledObject<ObjectType>::operator==(const PooledObject& other) const {
    return other.index == index;
}

template <typename ObjectType>
auto PooledObject<ObjectType>::operator<=>(const PooledObject<ObjectType>& other) const {
    return static_cast<int32_t>(static_cast<int64_t>(other.index) - static_cast<int64_t>(index));
}

template <typename ObjectType>
bool PooledObject<ObjectType>::is_valid() const {
    return index != 0xFFFFFFFF && pool != nullptr;
}

template <typename ObjectType>
PooledObject<ObjectType> ObjectPool<ObjectType>::emplace(ObjectType&& object) {
    auto handle = PooledObject<ObjectType>{0u, this};

    if (available_handles.empty()) {
        handle.index = static_cast<uint32_t>(objects.size());
        objects.emplace_back(std::move(object));

    } else {
        handle = available_handles.back();
        available_handles.pop_back();

        objects[handle.index] = std::move(object);
    }

    num_objects++;

    return handle;
}

template <typename ObjectType>
ObjectType& ObjectPool<ObjectType>::get_object(const PooledObject<ObjectType>& handle) {
    return objects[handle.index];
}

template <typename ObjectType>
eastl::vector<ObjectType>& ObjectPool<ObjectType>::get_data() {
    return objects;
}

template <typename ObjectType>
const eastl::vector<ObjectType>& ObjectPool<ObjectType>::get_data() const {
    return objects;
}

template <typename ObjectType>
ObjectType ObjectPool<ObjectType>::free_object(const PooledObject<ObjectType>& handle) {
    auto object = objects[handle.index];
    objects[handle.index] = {};
    available_handles.emplace_back(handle);

    num_objects--;

    return object;
}

template <typename ObjectType>
ObjectType& ObjectPool<ObjectType>::operator[](uint32_t index) {
    return objects[index];
}

template <typename ObjectType>
const ObjectType& ObjectPool<ObjectType>::operator[](uint32_t index) const {
    return objects[index];
}

template <typename ObjectType>
ObjectType ObjectPool<ObjectType>::free_object(uint32_t index) {
    return free_object(PooledObject<ObjectType>{.index = index, .pool = this});
}

template <typename ObjectType>
PooledObject<ObjectType> ObjectPool<ObjectType>::make_handle(uint32_t index_in) {
    return { index_in, this };
}

template <typename ObjectType>
template <typename CreateFunc, typename DestroyFunc>
ObjectPool<ObjectType>::ObjectPool(CreateFunc&& creator_in, DestroyFunc&& deleter_in) :
        creator{creator_in}, deleter{deleter_in} {}

template <typename ObjectType>
ObjectPool<ObjectType>::~ObjectPool() {
    for (auto& obj: objects) {
        deleter(std::move(obj));
    }
}

template <typename ObjectType>
PooledObject<ObjectType> ObjectPool<ObjectType>::create_object() {
    if (!available_handles.empty()) {
        auto obj = available_handles.back();
        available_handles.pop_back();

        num_objects++;

        return obj;
    }

    return emplace(creator());
}

template <typename ObjectType>
ObjectPool<ObjectType>::ObjectPool() : ObjectPool([]() { return ObjectType{}; }, [](ObjectType&& obj) {}) {

}
