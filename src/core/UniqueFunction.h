// UniqueFunction.h -- move-only type-erased callable (std::move_only_function is C++23).
#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace mc {

template <class Signature>
class UniqueFunction;

template <class R, class... Args>
class UniqueFunction<R(Args...)> {
public:
    UniqueFunction() = default;
    UniqueFunction(std::nullptr_t) {}

    template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, UniqueFunction> &&
                                                std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>>
    UniqueFunction(F&& f) : impl_(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}

    UniqueFunction(UniqueFunction&&) noexcept = default;
    UniqueFunction& operator=(UniqueFunction&&) noexcept = default;
    UniqueFunction(const UniqueFunction&) = delete;
    UniqueFunction& operator=(const UniqueFunction&) = delete;

    explicit operator bool() const { return impl_ != nullptr; }

    R operator()(Args... args) { return impl_->call(std::forward<Args>(args)...); }

private:
    struct Base {
        virtual ~Base() = default;
        virtual R call(Args&&... args) = 0;
    };
    template <class F>
    struct Impl final : Base {
        template <class G>
        explicit Impl(G&& g) : f(std::forward<G>(g)) {}
        R call(Args&&... args) override { return std::invoke(f, std::forward<Args>(args)...); }
        F f;
    };
    std::unique_ptr<Base> impl_;
};

} // namespace mc
