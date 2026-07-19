#pragma once
#include "TypeId.h"
#include "Archetype.h"
#include <vector>
#include <tuple>
#include <array>
#include <algorithm>
#include <cstddef>
#include <utility>

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
        // Column base pointers for the current archetype, one per Include, so
        // dereference is a stride-multiply instead of a per-row colIndex()
        // binary search per component. Valid under the existing contract (no
        // structural mutation while iterating — a push/migration would
        // reallocate the column and dangle these).
        size_t cachedArch_ = static_cast<size_t>(-1);
        std::array<std::byte*, sizeof...(Includes)> colBase_{};

        Iterator(std::vector<Archetype*>* m, size_t ai, size_t r)
            : matched_(m), archIdx_(ai), row_(r) { skipEmpty(); }

        void cacheColumns() {
            Archetype* arch = (*matched_)[archIdx_];
            size_t k = 0;
            ((colBase_[k++] = static_cast<std::byte*>(
                  arch->cols[arch->colIndex(typeId<Includes>())].at(0))), ...);
            cachedArch_ = archIdx_;
        }

        // Advance past exhausted archetypes; refresh column pointers when the
        // archetype changes (skipped archetypes are empty, so caching only
        // ever happens on one with rows).
        void skipEmpty() {
            while (archIdx_ < matched_->size() &&
                   row_ >= (*matched_)[archIdx_]->size()) {
                ++archIdx_;
                row_ = 0;
            }
            if (archIdx_ < matched_->size() && archIdx_ != cachedArch_)
                cacheColumns();
        }

        bool operator==(const Sentinel&) const { return archIdx_ >= matched_->size(); }
        bool operator!=(const Sentinel&) const { return archIdx_ < matched_->size(); }

        TupleRef operator*() const {
            return deref(std::index_sequence_for<Includes...>{});
        }

        template<size_t... Is>
        TupleRef deref(std::index_sequence<Is...>) const {
            Archetype* arch = (*matched_)[archIdx_];
            // sizeof(Includes) matches the column's registered element size
            // (Registry::add registers sizeof(T)), so the stride is static.
            return TupleRef(
                arch->entities[row_],
                *reinterpret_cast<Includes*>(colBase_[Is] + row_ * sizeof(Includes))...
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
