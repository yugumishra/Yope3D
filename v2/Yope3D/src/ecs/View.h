#pragma once
#include "TypeId.h"
#include "Archetype.h"
#include <vector>
#include <tuple>
#include <array>
#include <algorithm>

namespace ecs {

// Lazy iterable over all archetypes containing every type in Includes.
// Call .exclude<Ts...>() to additionally skip archetypes that contain any Ts.
// Iteration yields std::tuple<Entity, Includes&...> — components are refs into
// archetype storage, so mutations through them write back immediately.
//
// Construct via Registry::view<Ts...>(), not directly.
//
// Iterator validity: do NOT add/remove components or create/destroy entities
// while iterating — that can migrate entities to different archetypes.
template<typename... Includes>
class View {
public:
    using TupleRef = std::tuple<Entity, Includes&...>;

    explicit View(std::vector<Archetype*> matched)
        : matched_(std::move(matched)) {}

    // Filter out archetypes containing any of Excludes.
    // Returns a new View (moves matched_ out of *this); only callable on rvalue Views
    // so the fluent chain  reg.view<T,R>().exclude<S>()  lifetime-extends correctly.
    template<typename... Excludes>
    View<Includes...> exclude() && {
        if constexpr (sizeof...(Excludes) > 0) {
            std::array<TypeId, sizeof...(Excludes)> ids = { typeId<Excludes>()... };
            auto newEnd = std::remove_if(matched_.begin(), matched_.end(),
                [&](Archetype* arch) {
                    for (TypeId eid : ids)
                        if (arch->colIndex(eid) >= 0) return true;
                    return false;
                });
            matched_.erase(newEnd, matched_.end());
        }
        return std::move(*this);
    }

    // -------------------------------------------------------------------------
    // Iterator
    // -------------------------------------------------------------------------

    struct Sentinel {};

    struct Iterator {
        std::vector<Archetype*>* matched_;
        size_t archIdx_;
        size_t row_;

        Iterator(std::vector<Archetype*>* m, size_t ai, size_t r)
            : matched_(m), archIdx_(ai), row_(r) { skipEmpty(); }

        // Advance past exhausted archetypes.
        void skipEmpty() {
            while (archIdx_ < matched_->size() &&
                   row_ >= (*matched_)[archIdx_]->size()) {
                ++archIdx_;
                row_ = 0;
            }
        }

        bool operator==(const Sentinel&) const { return archIdx_ >= matched_->size(); }
        bool operator!=(const Sentinel&) const { return archIdx_ < matched_->size(); }

        TupleRef operator*() const {
            Archetype* arch = (*matched_)[archIdx_];
            Entity e = arch->entities[row_];
            // Expand Includes pack: dereference pointer into each component column.
            return TupleRef(
                e,
                *static_cast<Includes*>(
                    arch->cols[arch->colIndex(typeId<Includes>())].at(row_)
                )...
            );
        }

        Iterator& operator++() {
            ++row_;
            skipEmpty();
            return *this;
        }
    };

    Iterator begin() { return Iterator(&matched_, 0, 0); }
    Sentinel end()   { return {}; }

private:
    std::vector<Archetype*> matched_;
};

} // namespace ecs
