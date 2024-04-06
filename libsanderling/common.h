//
// Created by allan on 2024/3/30.
//

#pragma once

#define LOGURU_WITH_STREAMS 1

#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <map>
#include <memory>
#include <vector>
#include <loguru.hpp>
#include <boost/thread.hpp>
#include <shared_mutex>
#include <boost/bind/bind.hpp>
#include <boost/asio.hpp>
#include <unordered_set>
#include <type_traits>

#include <iostream>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-stack-address"

namespace eve {
    template<typename T>
    class Property {
        T _value;
        bool _isAuto = true;
        /**
         * Used for auto-property behavior.
         */
        std::function<const T &(const T &)> _autoGetter = [](const T &value) -> const T & { return value; };
        std::function<const T &(T &, const T &)> _autoSetter = [](T &value,
                                                                  const T &newValue) -> const T & { return value = newValue; };

        /**
         * Used for non-auto property behavior.
         */
        std::function<const T &()> _getter;
        std::function<const T &(const T &)> _setter;

        /**
         * Convenience methods for calling the actual setter/getters regardless of auto-property
         * or not.
         */
        const T &set(const T &value) { return _isAuto ? _autoSetter(_value, value) : _setter(value); }

        const T &get() { return _isAuto ? _autoGetter(_value) : _getter(); }

    public:

        struct AutoParams {
            std::function<const T &(const T &)> get = [](const T &value) { return value; };
            std::function<const T &(T &, const T &)> set = [](T &value, const T &newValue) { return value = newValue; };
        };

        struct Params {
            std::function<const T &()> get = []() -> const T & { throw new std::exception(); };
            std::function<const T &(const T &)> set = [](const T &value) -> const T & { throw new std::exception(); };
        };

        struct WrappedGetParams {
            T &get;
        };

        struct WrappedSetParams {
            T &set;
        };

        // Implicit conversion back to T.
        operator const T &() { return get(); }

        const T operator=(T other) { return set(other); }

        const T operator=(Property<T> other) { return set(other.get()); }

        Property<T> &operator++() { return set(get()++); }

        T operator++(int n) {
            return set(get() + (n != 0 ? n : 1));
        }

        Property<T> &operator--() { return set(get()--); }

        T operator--(int n) {
            return set(get() - (n != 0 ? n : 1));
        }

        const T &operator+=(const T &other) { return set(get() + other); }

        const T &operator-=(const T &other) { return set(get() - other); }

        const T &operator+(const T &other) { return get() + other; }

        friend const T &operator+(const T &first, Property<T> &other) { return first + other.get(); }

        const T &operator-(const T &other) { return get() - other; }

        friend const T &operator-(const T &first, Property<T> &other) { return first - other.get(); }

        const T &operator*(const T &other) { return get() * other; }

        friend const T &operator*(const T &first, Property<T> &other) { return first * other.get(); }

        const T &operator/(const T &other) { return get() / other; }

        friend const T &operator/(const T &first, Property<T> &other) { return first / other.get(); }

        friend std::ostream &operator<<(std::ostream &os, Property<T> &other) { return os << other.get(); }

        friend std::istream &operator>>(std::istream &os, Property<T> &other) {
            if (other._isAuto) {
                return os >> other._value;
            } else {
                T ref;
                os >> ref;
                other.set(ref);
                return os;
            }
        }

        // This template class member function template serves the purpose to make
        // typing more strict. Assignment to this is only possible with exact identical types.
        // The reason why it will cause an error is temporary variable created while implicit type conversion in reference initialization.
        template<typename T2>
        T2 &operator=(const T2 &other) {
            T2 &guard = _value;
            throw guard; // Never reached.
        }

        Property() {}

        Property(T &value) {
            _isAuto = false;
            _getter = [&]() -> const T & { return value; };
            _setter = [&](const T &newValue) -> const T & { return value = newValue; };
        }

        Property(AutoParams params) {
            _isAuto = true;
            _autoGetter = params.get;
            _autoSetter = params.set;
        }

        Property(Params params) {
            _isAuto = false;
            _getter = params.get;
            _setter = params.set;
        }

        Property(WrappedGetParams params) {
            _isAuto = false;
            auto get = params.get;
            _getter = [get]() { return get; };
            _setter = [](const T &newValue) -> const T & { throw new std::exception(); };
        }

        Property(WrappedSetParams params) {
            _isAuto = false;
            T &set = params.set;
            _getter = []() -> const T & { throw new std::exception(); };
            _setter = [&set](const T &newValue) { return set = newValue; };
        }
    };
}
#pragma GCC diagnostic pop