#pragma once
#include <iostream>
#include <vcruntime_typeinfo.h>
#include <vector>

#include "boost/pfr/core.hpp"
#include "boost/pfr/core_name.hpp"
#include "flecs.h"

template <typename>
struct inner;

template <template <typename> class outter_t, typename inner_t>
struct inner<outter_t<inner_t>> {
  typedef typename std::remove_cv<typename std::remove_reference<inner_t>::type>::type type;
};

template <typename _t>
using inner_t = typename inner<std::remove_reference_t<_t>>::type;

namespace Reflection {
namespace Detail {
template <class T>
struct is_std_vector {
  static constexpr bool value = false;
};

template <class T>
struct is_std_vector<std::vector<T>> {
  static constexpr bool value = true;
};

// Reusable reflection support for std::vector
template <typename Elem, typename Vector = std::vector<Elem>>
flecs::opaque<Vector, Elem> std_vector_support(flecs::world &world) {
  flecs::entity type = world.scope("VectorType").vector<Elem>();
  std::string name = std::string("opaque<vector<") + typeid(Elem).name() + ">>";
  type.set_name(name.c_str());
  return flecs::opaque<Vector, Elem>()
      .as_type(type)
      // Forward elements of std::vector value to serializer
      .serialize([](const flecs::serializer *s, const Vector *data) {
        for (const auto &el : *data) {
          s->value(el);
        }
        return 0;
      })
      // Return vector count
      .count([](const Vector *data) {
        return data->size();
      })
      // Resize contents of vector
      .resize([](Vector *data, size_t size) {
        data->resize(size);
      })
      // Ensure element exists, return pointer
      .ensure_element([](Vector *data, size_t elem) {
        if (data->size() <= elem) {
          data->resize(elem + 1);
        }

        return &data->data()[elem];
      });
}

template <typename T>
void GenerateImplicitReflectionBinds(flecs::world &world) {
  flecs::untyped_component cmp = world.component<T>();
  if (cmp.has<flecs::Type>() || cmp.has<EcsOpaque>()) {
    return;
  }

  if constexpr (is_std_vector<T>::value) {
    world.component<T>()
        .opaque(std_vector_support<typename T::value_type>);

    if constexpr (std::is_aggregate_v<typename T::value_type>) {
      GenerateImplicitReflectionBinds<typename T::value_type>(world);
    }
  } else {
    const auto names = boost::pfr::names_as_array<T>();
    T *dummy = nullptr;

    boost::pfr::for_each_field(*dummy, [&]<typename MemberType>(const MemberType &value, std::size_t i) {
      printf("Testing %s %s\n", names[i].data(), typeid(MemberType).name());
      if (!world.component<MemberType>().template has<flecs::Type>() && !world.component<MemberType>().template has<EcsOpaque>()) {
        if constexpr (std::is_aggregate_v<MemberType>) {
          GenerateImplicitReflectionBinds<MemberType>(world);
        } else if constexpr (is_std_vector<MemberType>::value) {
          world.component<MemberType>()
              .opaque(std_vector_support<typename MemberType::value_type>);

          if constexpr (std::is_aggregate_v<typename MemberType::value_type>) {
            GenerateImplicitReflectionBinds<typename MemberType::value_type>(world);
          }
        } else if constexpr (!std::is_pointer_v<MemberType>) {
          printf("Skipping %s\n", typeid(MemberType).name());
          return;
        }
      }
      const void *vptr = static_cast<const void *>(&value);
      ptrdiff_t offset = static_cast<const uint8_t *>(vptr) - reinterpret_cast<const uint8_t *>(dummy);
      cmp.member<MemberType>(names[i].data(), std::extent_v<MemberType>, offset);
    });
  }
}
} // namespace Detail

template <typename T>
void Register(flecs::world &world, bool singleton = false) {
  Detail::GenerateImplicitReflectionBinds<T>(world);
}
} // namespace Reflection
