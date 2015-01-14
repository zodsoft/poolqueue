/*
Copyright 2015 Shoestring Research, LLC.  All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <exception>
#include <string>
#include <typeinfo>
#include <utility>

namespace poolqueue {

   namespace detail {

      class Any {
         class Holder {
         public:
            virtual ~Holder() {}
            virtual const std::type_info& type() const = 0;
            virtual Holder *copy() const = 0;
         };

         template<typename T, bool CopyConstructible>
         class HolderT : public Holder {
            T value_;
            
         public:
            HolderT(const T& value) : value_(value) {
            }

            HolderT(T&& value) : value_(std::forward<T>(value)) {
            }

            const std::type_info& type() const {
               return typeid(T);
            }

            Holder *copy() const {
               return new HolderT(value_);
            }

            T& get() {
               return value_;
            }

            const T& get() const {
               return value_;
            }
         };

         // Specialization for non-copy-constructible values.
         template<typename T>
         class HolderT<T, false> : public Holder {
            T value_;
            
         public:
            HolderT(T&& value) : value_(std::forward<T>(value)) {
            }

            const std::type_info& type() const {
               return typeid(T);
            }

            Holder *copy() const {
               // Copying a Promise::Value is necessary if a then()
               // or except() callback returns a Promise. When the
               // returned Promise is resolved, the value set must
               // be copy-constructible. Noncopyable values can be
               // used in all other known internal cases.
               throw std::runtime_error("Promise contains noncopyable value");
            }

            T& get() {
               return value_;
            }

            const T& get() const {
               return value_;
            }
         };
            
         Holder *holder_;
      public:
         Any() : holder_(nullptr) {
         }

         ~Any() {
            delete holder_;
         }

         // Copy constructor and assignment.
         Any(const Any& other) : holder_(other.holder_ ? other.holder_->copy() : other.holder_) {
         }

         Any& operator=(const Any& other) {
            return swap(Any(other));
         }

         // Move construction and assignment.
         Any(Any&& other) {
            holder_ = nullptr;
            swap(other);
         }

         Any& operator=(Any&& other) {
            return swap(other);
         }

         // Value construction and assignment. Use SFINAE to
         // exclude matching an Any argument.
         template<typename T,
                  typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Any>::value, T>::type>
         Any(T&& value) : holder_(new HolderT<typename std::decay<T>::type, std::is_copy_constructible<typename std::decay<T>::type>::value>(std::forward<T>(value))) {
         }
            
         template<typename T,
                  typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Any>::value, T>::type>
         Any& operator=(T&& value) {
            return swap(Any(std::forward<T>(value)));
         }

         // Swap (both lvalue and rvalue).
         Any& swap(Any& other) {
            std::swap(holder_, other.holder_);
            return *this;
         }

         Any& swap(Any&& other) {
            std::swap(holder_, other.holder_);
            return *this;
         }

         // Query wrapped type.
         const std::type_info& type() const {
            return holder_ ? holder_->type() : typeid(void);
         }

         bool empty() const {
            return !holder_;
         }
            
         // Value cast for non-const instance.
         template<typename T>
         T cast() {
            typedef typename std::decay<T>::type DecayType;
            constexpr bool isCopyable = std::is_copy_constructible<DecayType>::value;
            auto *holder = dynamic_cast<HolderT<DecayType, isCopyable> *>(holder_);
            if (!holder)
               throw std::bad_cast();
            return static_cast<T>(holder->get());
         }

         // Const reference cast for const instance.
         template<typename T>
         const T& cast() const {
            typedef typename std::decay<T>::type DecayType;
            constexpr bool isCopyable = std::is_copy_constructible<DecayType>::value;
            auto *holder = dynamic_cast<const HolderT<DecayType, isCopyable> *>(holder_);
            if (!holder)
               throw std::bad_cast();
            return static_cast<const T&>(holder->get());
         }
      };

      // Enable idiomatic swap.
      inline void swap(Any& a, Any& b) {
         a.swap(b);
      }
         
      // Define trait templates for functions and member functions.
      // See:
      //  http://functionalcpp.wordpress.com/2013/08/05/function-traits/
      //  https://smellegantcode.wordpress.com/tag/c11/
      template<typename F> struct FunctionTraits;
      template<typename T> struct MemberFunctionTraits;

      // Specializations for 0 and 1 argument functions.
      template<typename R>
      struct FunctionTraits<R()> {
         typedef R ResultType;
         typedef void ArgumentType;
      };

      template<typename R, typename A>
      struct FunctionTraits<R(A)> {
         typedef R ResultType;
         typedef A ArgumentType;
      };

      // Adapt function pointers.
      template<typename R>
      struct FunctionTraits<R(*)()> : public FunctionTraits<R()> {
      };

      template<typename R, typename A>
      struct FunctionTraits<R(*)(A)> : public FunctionTraits<R(A)> {
      };

      // Specializations for 0 and 1 argument member functions.
      template<typename R, typename C>
      struct MemberFunctionTraits<R (C::*)() const> {
         typedef R ResultType;
         typedef void ArgumentType;
      };

      template<typename R, typename C, typename A>
      struct MemberFunctionTraits<R (C::*)(A) const> {
         typedef R ResultType;
         typedef A ArgumentType;
      };

      // Functor traits.
      template<typename F>
      class FunctorTraits {
         typedef typename std::decay<F>::type PlainF;
         typedef MemberFunctionTraits<decltype(&PlainF::operator())> traits;
      public:
         typedef typename traits::ResultType ResultType;
         typedef typename traits::ArgumentType ArgumentType;
      };

      // Combine function and functor traits.
      template<typename F>
      struct CallableTraits : public std::conditional<std::is_function<typename std::remove_pointer<F>::type>::value, FunctionTraits<F>, FunctorTraits<F> >::type {
      };

      class bad_cast : public std::bad_cast {
         const std::type_info& from_;
         const std::type_info& to_;
         std::string message_;
      public:
         bad_cast(const std::type_info& from, const std::type_info& to)
            : from_(from)
            , to_(to) {
            message_ = std::string("failed cast from ") + from_.name() + " to " + to_.name();
         }

         const std::type_info& from() const {
            return from_;
         }

         const std::type_info& to() const {
            return to_;
         }
            
         virtual const char *what() const noexcept {
            return message_.c_str();
         }
      };

      class CallbackWrapper {
      public:
         virtual ~CallbackWrapper() {}

         virtual bool hasRvalueArgument() const = 0;
         virtual bool hasExceptionPtrArgument() const = 0;
         virtual Any operator()(Any&&) const = 0;
      };

      // Template subclass wrapper for R f(A).
      template<typename F, typename R, typename A, bool AnyArg>
      class CallbackWrapperT : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return std::is_rvalue_reference<A>::value;
         }

         bool hasExceptionPtrArgument() const {
            return std::is_convertible<A, const std::exception_ptr&>::value;
         }

         Any operator()(Any&& a) const {
            typedef typename std::conditional<std::is_rvalue_reference<A>::value, A&&, const A&>::type type;
            if (a.type() != typeid(typename std::decay<A>::type))
               throw bad_cast(a.type(), typeid(A));
            return f_(a.cast<type>());
         }
      };

      // Specialize for void f(A).
      template<typename F, typename A>
      class CallbackWrapperT<F, void, A, false> : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return std::is_rvalue_reference<A>::value;
         }

         bool hasExceptionPtrArgument() const {
            return std::is_convertible<A, const std::exception_ptr&>::value;
         }

         Any operator()(Any&& a) const {
            typedef typename std::conditional<std::is_rvalue_reference<A>::value, A&&, const A&>::type type;
            if (a.type() != typeid(typename std::decay<A>::type))
               throw bad_cast(a.type(), typeid(A));
            f_(a.cast<type>());
            return Any();
         }
      };
         
      // Specialize for R f().
      template<typename F, typename R>
      class CallbackWrapperT<F, R, void, false> : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return false;
         }

         bool hasExceptionPtrArgument() const {
            return false;
         }

         Any operator()(Any&&) const {
            return f_();
         }
      };
         
      // Specialize for void f().
      template<typename F>
      class CallbackWrapperT<F, void, void, false> : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return false;
         }

         bool hasExceptionPtrArgument() const {
            return false;
         }

         Any operator()(Any&&) const {
            f_();
            return Any();
         }
      };

      // Specialize for R f(const Any&) or R f(Any&&).
      template<typename F, typename R, typename A>
      class CallbackWrapperT<F, R, A, true> : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return std::is_rvalue_reference<A>::value;
         }

         bool hasExceptionPtrArgument() const {
            return false;
         }

         Any operator()(Any&& a) const {
            return f_(static_cast<A>(a));
         }
      };

      // Specialize for void f(const Any&) or void f(Any&&).
      template<typename F, typename A>
      class CallbackWrapperT<F, void, A, true> : public CallbackWrapper {
         typename std::remove_reference<F>::type f_;
      public:
         CallbackWrapperT(F&& f) : f_(std::forward<F>(f)) {}

         bool hasRvalueArgument() const {
            return std::is_rvalue_reference<A>::value;
         }

         bool hasExceptionPtrArgument() const {
            return false;
         }

         Any operator()(Any&& a) const {
            f_(static_cast<A>(a));
            return Any();
         }
      };

      template<typename F>
      CallbackWrapper *makeCallbackWrapper(F&& f) {
         typedef typename CallableTraits<F>::ResultType R;
         typedef typename CallableTraits<F>::ArgumentType A;

         constexpr bool isConst = std::is_const<typename std::remove_reference<A>::type>::value;
         constexpr bool isLvalue = std::is_lvalue_reference<A>::value;
         static_assert(isConst || !isLvalue,
                       "Promise callbacks cannot take non-const lvalue reference argument.");

         constexpr bool isValue = std::is_same<typename std::decay<A>::type, Any>::value;
         constexpr bool isValueConstRef = std::is_same<A, const Any&>::value;
         constexpr bool isValueRvalueRef = std::is_rvalue_reference<A>::value;
         static_assert(!isValue || isValueConstRef || isValueRvalueRef,
                       "Promise callback can take const Value& or Value&&.");
            
         constexpr bool isException = std::is_same<typename std::decay<A>::type, std::exception_ptr>::value;
         constexpr bool isExceptionConstRef = std::is_same<A, const std::exception_ptr&>::value;
         static_assert(!isException || isExceptionConstRef,
                       "Promise callback can take const std::exception_ptr&.");
            
         return new CallbackWrapperT<F, typename std::decay<R>::type, A, isValue>(std::forward<F>(f));
      }

      // Helper for Promise::then() to deduce an elided onReject argument.
      template<typename F>
      struct IsDefaultExceptionCallback {
         IsDefaultExceptionCallback(const F&) {}
         operator bool() const { return false; }
      };

      template<>
      struct IsDefaultExceptionCallback<std::function<void(const std::exception_ptr&)> > {
         const std::function<void(const std::exception_ptr&)>& f_;
         IsDefaultExceptionCallback(const std::function<void(const std::exception_ptr&)>& f) : f_(f) {}
         operator bool() const { return !f_; }
      };

   }
}
