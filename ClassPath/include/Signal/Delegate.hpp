#ifndef hpp_Delegate_hpp
#define hpp_Delegate_hpp

// We need types
#include "../Types.hpp"
// We need Strings too
#include "../Strings/Strings.hpp"
// We need Containers for the index list
#include "../Container/Container.hpp"

/** Some application need dynamic logic, requiring dynamic delegating calls to objects's method or functions.
    This can be done with usual callback based design (like most of this library does), or with the help of delegates.
    Typically, Delegate stores all the required information to call a function (on an object or not).
    Used with Slot, you can dynamically attach one or more "reactors" to a single source, dynamically and have the behaviour of
    your application evolve at runtime simply.

    Additionally, using Delegate in your class allow introspection features provided your class derives from Introspect base class
    (useful for runtime based dumping / setting the behaviour without knowning the class interface)
    @sa Delegate */
namespace Signal
{
    // Forward declare the types
    template <typename Sig> class Invoker;
    template <typename Sig> struct Delegate;

    /** The string class we are using */
    typedef Strings::FastString String;


    class PropertyBase;

    /** A listener on a property change. */
    struct PropertyWatcher
    {
        /** Called upon property modification. */
        virtual void modified(const PropertyBase * property) = 0;
        virtual ~PropertyWatcher() {}
    };

    /** The property base.
        This is a generic base interface for manipulating any type.
        This is supposed to be used by the Introspect class, you should not have to use this. */
    class PropertyBase
    {
    protected:
        /** How to store the property value or reference */
        void * pointer;
        /** The property type when captured */
        const char * type;
        /** The captured property name */
        const char * name;
        /** The property getter / setter hook */
        PropertyWatcher * watcher;

        /** Constructor is only accessible from child class */
        PropertyBase(void * pointer, const char * type, const char * name, PropertyWatcher * watcher = 0) : pointer(pointer), type(type), name(name), watcher(watcher) {}

    public:
        /** Get the signature for this holder */
        const char * getType() const { return type; }
        /** Get the name of the member this holder is linked with (if provided) */
        const char * getName() const { return name; }
        /** Dump the value of the property */
        virtual String dump() const { return String::Print("(%s *)%p", type, pointer); }
        /** Check if both type match */
        bool isMatching(const PropertyBase & other) const { return strcmp(type, other.type) == 0; }
        /** Assign this property from another property, the type is ignored */
        void assign(const PropertyBase & other) { pointer = other.pointer; name = other.name; if (watcher) watcher->modified(this); }
        /** Clear the property */
        void clear() { pointer = 0; if (watcher) watcher->modified(this); }
        /** Required virtual destructor */
        virtual ~PropertyBase() {}
    };

    /** Contains some useful type to string converter for the dynamic dump interface function in Introspect class */
    namespace ConvertType
    {
        inline String dumpType(const char* type, void * p) { return String::Print("(%s *)%p", type, p); }
        inline String dumpType(const char* type, const char * s) { return s ? String::Print("\"%s\"", s) : String("(nil)"); }
        inline String dumpType(const char* type, uint64 s) { return String::Print("%llu", s); }
        inline String dumpType(const char* type, int64 s) { return String::Print("%lld", s); }
        inline String dumpType(const char* type, int s)   { return String::Print("%d", s); }
        inline String dumpType(const char* type, unsigned s) { return String::Print("%u", s); }
        inline String dumpType(const char* type, uint16 s) { return String::Print("%hu", s); }
        inline String dumpType(const char* type, int16 s) { return String::Print("%hd", s); }
        inline String dumpType(const char* type, double s) { return String::Print("%g", s); }
        inline String dumpType(const char* type, const String & s) { return String::Print("\"%s\"", (const char*)s); }

        // Default is to fail
        template <typename T>
        inline String dumpType(const char * value, const T & s) { return ""; }



        inline bool setPropertyUnsafe(const char * value, void *& p)        { int c = 0; uint64 pv = (uint64)String(value).parseInt(0, &c); if (c) { p = (void*)pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, const char *& s)  { return false; } // No allocation is possible here
        inline bool setPropertyUnsafe(const char * value, uint64& s)        { int c = 0; uint64 pv = (uint64)String(value).parseInt(0, &c); if (c) { s = pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, int64& s)         { int c = 0; int64 pv = (int64)String(value).parseInt(0, &c); if (c) { s = pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, int& s)           { int c = 0; int64 pv = (int64)String(value).parseInt(0, &c); if (c && (pv & 0xFFFFFFFFULL) == (uint64)pv) { s = (int)pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, unsigned& s)      { int c = 0; uint64 pv = (uint64)String(value).parseInt(0, &c); if (c && (pv & 0xFFFFFFFFULL) == pv) { s = (unsigned)pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, uint16& s)        { int c = 0; uint64 pv = (uint64)String(value).parseInt(0, &c); if (c && (pv & 0xFFFFULL) == pv) { s = pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, int16& s)         { int c = 0; int64 pv = (uint64)String(value).parseInt(0, &c); if (c && (pv & 0xFFFFULL) == (uint64)pv) { s = pv; return true; } return false; }
        inline bool setPropertyUnsafe(const char * value, double& s)        { s = (double)String(value); return true; }
        inline bool setPropertyUnsafe(const char * value, float& s)         { s = (float)(double)String(value); return true; }
        inline bool setPropertyUnsafe(const char * value, String & s)       { s = String(value); return true; }

        // Default is to fail
        template <typename T>
        inline bool setPropertyUnsafe(const char * value, T & s) { return false; }


    }


    /** The property class is to member as delegate is to method.
        It remembers the member name, its type, and allow fetching and setting it without knowing about the underlying type.
        It can be reset to any other compatible type.

        Typically, you'll use it this way:
        @code
        typedef Property<String> StringProp;
        class Test
        {
            String m;
            StringProp property;

        public:
            StringProp & getProperty() { return property; }
            Test() : m("secret"), property(StringProp::From(m)) {}
        };

        // Simple transition
        String i = "Bob";
        StringProp y = StringProp::From(i);
        String res = y(); // res == "Bob"
        y("Alice"); // i == "Alice"

        // Access to private members
        Test t;
        String res = t.getProperty()(); // res == "secret"
        t.getProperty()("password"); // t.m == "password"
        @endcode

        It's also very useful for introspection, if you use DeclProp and DeclPropRef macro:
        @code
        typedef Property<int> IntProp;
        struct MyClass : public Introspect<MyClass> // Required for introspection, or if using DeclProp macro
        {
            DeclPropImpl(int, a); // Equivalent of "int a;"
            int b;
            DeclProp(int, b); // No equivalent here, the property is declared with name "property_b"

            MyClass() : AssignProp(a), AssignProp(b) {}
        };

        // Use introspection
        printf("%s\n", MyClass::getProperties()[0]->getName()); // Output "a"
        printf("%s\n", MyClass::getProperties()[1]->getName()); // Output "b"
        printf("%s\n", MyClass::getProperties()[0]->getType()); // Output "int"
        MyClass obj;
        obj.assignProperty(0, 3); // Assign the property, equivalent to "obj.a = 3"

        // Get the property
        IntProp::Type t; // or simply int t;
        obj.getProperty(0, &t); // t is set to 3
        t = 6;
        obj.setProperty(0, &t); // obj.a is set to 6

        // Get the object interface as if it wasn't using properties
        printf("%s\n", obj.dumpInterface()); // Output "struct MyClass { int a; int b; };"
        @endcode */
    template <typename U>
    struct Property : public PropertyBase
    {
    public:
        /** The manipulated type */
        typedef U   Type;

    private:
         /** Make sure bool conversion is failing */
        void badBoolType() {} typedef void (Property::*badBoolPtr)();

         // Capture the member
     public:
         /** Capture the given type */
         static Property From(Type & t) { return Property(&t, 0); }
         /** Capture the given type and name */
         static Property From(Type & t, const char * name) { return Property(&t, name); }

         /** Construction */
         Property() : PropertyBase(0, type(), 0) {}
         /** Copy construction */
         Property(const Property & other, const char * name) : PropertyBase(other.pointer, other.type, name) {}

         /** Simple wrapper used in macro code, useless for you (allow member access to static function) */
         static inline Property & property() { static Property t; return t; }

         // Usage
     public:
         /** Define a property watcher */
         inline void setWatcher(PropertyWatcher * watcher) { this->watcher = watcher; }
         /** Use as a function */
         inline const Type & operator() () const { return *(Type*)pointer; }
         /** Use as a function */
         inline void operator() (const Type & o) const { *(Type*)pointer = o; if (watcher) watcher->modified(this); }

         /** Fetch with the (provided) type structure */
         inline void get(Type & out) const { if (!pointer) return; out = *(Type*)pointer; }
         /** Change with the (provided) type structure */
         inline void set(const Type & in) const { if (!pointer) return; *(Type*)pointer = in; if (watcher) watcher->modified(this); }

         /** Fetch virtually */
         void getVirt(void * args) const { Type * out = static_cast<Type *>(args); if (out) get(*out); }
         /** Set virtually */
         void setVirt(const void * args) const { const Type * in = static_cast<const Type *>(args); if (in) set(*in); }

         /** Capture the signature as a string usable for registering the delegate */
         static const char* type()
         {
  #ifdef _MSC_VER
             static char func[ sizeof(__FUNCTION__) - 2]; if (!*func) { strcpy(func, strstr(__FUNCTION__, "<") + 1); *strstr(func, ">") = 0; }
  #else
             static char func[ sizeof(__PRETTY_FUNCTION__) - 2];
             if (!*func)
             {
                 Strings::FastString name(__PRETTY_FUNCTION__), base = name.fromTo("<", ">");
                 memcpy(func, (const char*)base, base.getLength()+1);
             }
  #endif
             return func;
         }
         /** Dump the value of the property */
         virtual String dump() const { return ConvertType::dumpType(PropertyBase::type, *(Type*)pointer); }
         /** Set the value of this property from a textual value. You should avoid this if possible */
         virtual bool setPropertyFromTextUnsafe(const char * text)
         {
             if (ConvertType::setPropertyUnsafe(text, *(Type*)pointer))
             {
                 if (watcher) watcher->modified(this);
                 return true;
             }
             return false;
         }

         /** Check if valid with simple test pattern */
         bool operator ! () const { return pointer != 0; }
         /** When used like in a bool context, this is called */
         inline operator badBoolPtr() const { return !static_cast<Property const&>( *this ) ? 0 : &Property::badBoolType; }

         /** Construction internal */
         Property(void* pointer, const char * name) : PropertyBase(pointer, type(), name) {}
    };


    /** The base structure for argument storing (used for invoking the delegate).
        A derived class is made, that captures both the return type of the call and the argument for calling.
        However, since this class is specialized for each delegate type, you can use the Delegate::ArgType type to get the
        right specialization.
        The return type is always called "ret", and the argument are called respectively "a1", "a2", "a3", "a4", "a5"
        @todo Link this with Variant to allow RPC like behaviour */
    struct Arguments
    {
        /** The number of arguments stored */
        const size_t count;

        /** Construct the arguments */
        Arguments(const size_t count = 0) : count(count) {}
        /** Clone this object (for example when capturing arguments to call later on) */
        virtual Arguments * Clone () { return new Arguments(count); }

        /** This is required for polymorphic detection of type */
        virtual ~Arguments() {}
    };



    /** The delegate holder.
        This is used for introspection and dynamic delegate plumbing. */
    class Holder
    {
        // Disabled construction from outside
    protected:
        /** A pointer to the object */
        void *       objPtr;
        /** The function pointer */
        void *       funcPtr;
        /** The signature when captured */
        const char * signature;
        /** The captured's function name */
        String       linkedTo;

        /** Constructor is only accessible from child class */
        Holder(void* objPtr, void * funcPtr, const char * sig, const char * linkedTo) : objPtr(objPtr), funcPtr(funcPtr), signature(sig), linkedTo(linkedTo ? linkedTo : "") {}

        // Common interface
    public:
        /** Get the signature for this holder */
        const char * getSignature() const { return signature; }
        /** Get the name of the function this holder is linked with (if provided) */
        const char * getLinkedTo() const { return linkedTo; }
        /** Check if both signature match */
        bool isMatching(const Holder & other) const { return strcmp(signature, other.signature) == 0; }
        /** Assign this holder from another holder, the signature is ignored */
        void assign(const Holder & other) { objPtr = other.objPtr; funcPtr = other.funcPtr; linkedTo = other.linkedTo; }
        /** Clear the delegate */
        void clear() { objPtr = 0; funcPtr = 0; linkedTo = ""; }
        /** The virtual invoke method.
            Usually, you should not use this, as it's not as fast as the direct version that knows the types correctly. */
        virtual void invokeVirt(Arguments * args) const = 0;
        /** Required virtual destructor */
        virtual ~Holder() {}
    };


#ifdef DOXYGEN
/** Internal documentation for library developers, does not exist in reality */
namespace InternalForDev {
#endif
    /** Build a default value that's returned when invoking a non-assigned delegate
        If you need to specialize a default value for any type, you need to specialize this template like this:
        @code
        template<> struct BuildDefault<YourType, false>
        {
            static inline const YourType & ret() { static YourType t[whatever here to construct a default value]; return t; }
            typedef YourType Type;
        };
        @endcode */
    template <typename T, bool isPOD = false>
    struct BuildDefault
    {
        static inline const T & ret() { static T t; return t; }
        static inline const T & trueRet() { return ret(); }
        typedef T Type;
    };

    template <typename T>
    struct BuildDefault<T, true>
    {
        static inline T ret() { return 0; }
        static inline T trueRet() { return 0; }
        typedef T Type;
    };

    template <>
    struct BuildDefault<void, false>
    {
        struct Empty{ Empty(void){} };
        typedef Empty Type;
        static inline Type ret() { return Empty(); }
        static inline void trueRet() {  }
    };
    /** Get the default ret value for type T */
    #define GetDefaultRet(T) BuildDefault<T, IsPOD<T>::result == 1>::ret()
    #define GetDefaultTrueRet(T) BuildDefault<T, IsPOD<T>::result == 1>::trueRet()
    #define FilterRet(T) typename BuildDefault<T, IsPOD<T>::result == 1>::Type

    // This uglyness is for handling function returning void that should not affect the "Arguments" return member
    // because even for template code, "T ret = func()" is invalid when T = void
    template <typename T>
    struct VoidObjRet
    {
        typedef FilterRet(T) Ret;
        template <typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func) { ret = (*Func)(thisPtr); }
        template <typename Arg1, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg) { ret = (*Func)(thisPtr, arg); }
        template <typename Arg1, typename Arg2, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2) { ret = (*Func)(thisPtr, arg1, arg2); }
        template <typename Arg1, typename Arg2, typename Arg3, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3) { ret = (*Func)(thisPtr, arg1, arg2, arg3); }
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4) { ret = (*Func)(thisPtr, arg1, arg2, arg3, arg4); }
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5) { ret = (*Func)(thisPtr, arg1, arg2, arg3, arg4, arg5); }
    };

    template <>
    struct VoidObjRet<void>
    {
#if _MSC_VER
        typedef BuildDefault<void, IsPOD<void>::result == 1>::Type Ret;
#else
        typedef FilterRet(void) Ret;
#endif
        template <typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func) { (*Func)(thisPtr); }
        template <typename Arg1, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg) { (*Func)(thisPtr, arg); }
        template <typename Arg1, typename Arg2, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2) { (*Func)(thisPtr, arg1, arg2); }
        template <typename Arg1, typename Arg2, typename Arg3, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3) { (*Func)(thisPtr, arg1, arg2, arg3); }
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4) { (*Func)(thisPtr, arg1, arg2, arg3, arg4); }
        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename FuncSig>
        static inline void perform(Ret & ret, void * thisPtr, FuncSig Func, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5) { (*Func)(thisPtr, arg1, arg2, arg3, arg4, arg5); }
    };
#ifdef DOXYGEN
}
#endif


    /** The delegate class.
        @note Only the Delegate with 5 arguments is documented, but any lower argument count combination is provided too.

        This class is used to remember all that's required to call a function or method.
        This is optimized for speed, being at the time of writing 2x faster than a std::function equivalent
        (and approximately twice slower than direct function pointer calling).
        This is the primitive used for Source and Sink base.

        You'll likely prefer using Delegate when you only need a single connection/action when calling it.
        For example, for a "buttonClicked" action, Source/Sink are more appropriate, while for a "consumeData" action,
        a Delegate is preferred.


        Delegates are type safe (no conversion is done here, so safety is ensured).
        Creating a Delegate class resumes to a typedef (so it's easier to use), and assignment of the function or method to the delegate.
        This can be done manually, or by using the macros provided.
        Using macro also gives some introspection features.

        The basic idea comes from Sergey Ryazanov, althrough is completely rewritten and enhanced.

        Usage is like this:
        @code
            // Example function and methods that could be called
            bool testFunc(float a, float b) { return a+b > 3.4f; }
            struct A {
               int a;
               int me(const char * text) { return printf("Hello %s is %d\n", text, a); }
            };

            A obj; obj.a = 2;

            typedef Delegate<bool(float, float)> BFF;
            typedef Delegate<int(const char*)>   ICC;

            // Manual usage
            BFF d1 = BFF::From<&testFunc>();
            ICC d2 = ICC::From<A, &A::me>(&obj);

            // Then call the delegates
            d1(3, 4);
            d2("world");


            // Macro based usage (this also remembers the delegate
            // origin function name that can be used to introspect the object)
            // If obj has a "void registerDelegate(const char * name, const char * sig)" method, it'll be called automatically
            BFF d3 = MakeDelFunc(BFF, testFunc);
            ICC d4 = MakeDel(ICC, A, me, obj);
            // Declare and set
            BuildDelFunc(BFF, d5, testFunc); // Equivalent of declare and set
            BuildDel(ICC, d6, A, me, obj);   // Equivalent of declare and set

            // Using macro also provides numerours advantages as it captures the names of each function/member and
            // this is accessible for introspection
            struct MyClass : public Introspect<MyClass> // Required for introspection, or if using DeclDel macro
            {
                int a;
                DeclDel(BFF, b); // Equivalent of "BFF b;"
            private:
                DeclDel(ICC, other); // Equivalent of "ICC other;"
            };

            // Use introspection
            printf("%s\n", MyClass::getDelegates()[0]->getName()); // Output "b"
            printf("%s\n", MyClass::getDelegates()[0]->getSignature()); // Output "bool(float, float)"
            MyClass obj;
            obj.assignDelegate(0, d3); // Assign the delegate, equivalent to "obj.b = d3"
            printf("%s\n", obj.getDelegate(0)->getLinkedTo()); // Output "testFunc"

            // Invoke the delegate
            BFF::ArgType args(2.14f, 1.001595f);
            obj.invokeDelegate(0, args); // Equivalent to "obj.b(2.14f, 1.001595f)"

            // Get the object interface as if it wasn't using delegates
            printf("%s\n", obj.dumpInterface()); // Output "struct MyClass { bool b(float, float); int other(const char*); };"
        @endcode */
    template <typename Return, typename A1, typename A2, typename A3, typename A4, typename A5>
    struct Delegate<Return (A1, A2, A3, A4, A5)> : public Holder
    {
        // Type definition and enumeration
    public:
        /** The ReturnType so it's accessible from outside */
        typedef Return ReturnType;
        /** The signature type */
        typedef ReturnType (*SignatureType)(A1, A2, A3, A4, A5);
        /** The argument type */
        struct ArgType : public Arguments
        {
            FilterRet(Return) ret;
            A1 a1; A2 a2; A3 a3; A4 a4; A5 a5;
            /** Construct the argument array */
            ArgType(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) : Arguments(5), ret(GetDefaultRet(Return)), a1(a1), a2(a2), a3(a3), a4(a4), a5(a5) {}
            Arguments * Clone () { return new ArgType(a1, a2, a3, a4, a5); }
        };
   private:
        /** Make sure bool conversion is failing */
        void badBoolType() {} typedef void (Delegate::*badBoolPtr)();

        // Members
    private:
        /** The Stub pointer is of this type */
        typedef ReturnType (*StubType)(void *, A1, A2, A3, A4, A5);

        /** Capture the member name if provided */
        const char * name;



        // Capture the functions and method
    public:
        /** Capture a free function for this delegate */
        template <ReturnType (*Func)(A1, A2, A3, A4, A5)>
        static Delegate From() { return Stub(0, &funcStub<Func>); }

        /** Capture a member function for this delegate */
        template <class T, ReturnType (T::*Func)(A1, A2, A3, A4, A5)>
        static Delegate From(T* obj) { return Stub(obj, &methodStub<T, Func>); }

        /** Capture a const member function for this delegate */
        template <class T, ReturnType (T::*Func)(A1, A2, A3, A4, A5) const>
        static Delegate FromConst(T const* obj) { return Stub(const_cast<T*>(obj), &constMethodStub<T, Func>); }




        /** Capture a free function for this delegate */
        template <ReturnType (*Func)(A1, A2, A3, A4, A5)>
        static Delegate FromEx(const char * funcName) { return Stub(0, &funcStub<Func>, funcName); }

        /** Capture a member function for this delegate */
        template <class T, ReturnType (T::*Func)(A1, A2, A3, A4, A5)>
        static Delegate FromEx(T* obj, const char * methodName) { return Stub(obj, &methodStub<T, Func>, methodName); }

        /** Capture a const member function for this delegate */
        template <class T, ReturnType (T::*Func)(A1, A2, A3, A4, A5) const>
        static Delegate FromConstEx(T const* obj, const char * methodName) { return Stub(const_cast<T*>(obj), &constMethodStub<T, Func>, methodName); }

        /** Construction */
        Delegate() : Holder(0, 0, type(), 0), name(0) {}
        /** Copy construction */
        Delegate(const Delegate & other, const char * name) : Holder(other), name(name) {}

        /** Simple wrapper used in macro code, useless for you (allow member access to static function) */
        static inline Delegate & delegate() { static Delegate t; return t; }

        // Usage
    public:
        /** Use as a function */
        inline ReturnType operator() (A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) const { return funcPtr ? (*(StubType)funcPtr)(objPtr, a1, a2, a3, a4, a5) : GetDefaultTrueRet(ReturnType); }
        /** Invoke with the (provided) ArgType structure */
        inline void invoke(ArgType & args) const { if (!funcPtr) return; VoidObjRet<Return>::template perform<A1, A2, A3, A4, A5, StubType>(args.ret, objPtr, (StubType)funcPtr, args.a1, args.a2, args.a3, args.a4, args.a5); }
        /** Invoke virtually */
        void invokeVirt(Arguments * args) const { ArgType * _args = dynamic_cast<ArgType *>(args); if (_args) invoke(*_args); }

        /** Capture the signature as a string usable for registering the delegate */
        static const char* type()
        {
#ifdef _MSC_VER
            static char func[ sizeof(__FUNCTION__) - 2]; if (!*func) { strcpy(func, strstr(__FUNCTION__, "<") + 1); *strstr(func, ">") = 0; }
#else
            static char func[ sizeof(__PRETTY_FUNCTION__) - 2];
            if (!*func)
            {
                Strings::FastString name(__PRETTY_FUNCTION__), base = name.fromTo("<", ">"), val;
                if (name.fromFirst("with "))
                {   // static const char* Signal::Delegate<Return(A1, A2, A3, A4, A5)>::type() [with Return = int; A1 = int; A2 = int; A3 = char; A4 = int; A5 = char]
                    name = name.fromFirst("with Return = ");
                    val = name.splitFrom("; "); if (!val) val = name.splitFrom("]");
                    base.findAndReplace("Return(", val + "(");
                    while(name) { const Strings::FastString & key = name.splitFrom(" = "); val = name.splitFrom("; "); if (!val) val = name.splitFrom("]"); base.findAndReplace(key, val); }
                }
                memcpy(func, (const char*)base, base.getLength()+1);
            }
#endif
            return func;
        }
        /** Get the member name for this delegate (if provided) */
        const char * getMemberName() const { return name; }
        /** Check if valid with simple test pattern */
        bool operator ! () const { return funcPtr != 0; }
        /** When used like in a bool context, this is called */
        inline operator badBoolPtr() const { return !static_cast<Delegate const&>( *this ) ? 0 : &Delegate::badBoolType; }

        // Helpers
    public:
        /** Make a stub from a method */
        static Delegate Stub(void* object, StubType stubPtr, const char * linkedTo = 0, const char * name = 0) { return Delegate(object, stubPtr, linkedTo, name); }
        /** Simple function caller */
        template <ReturnType (*Method)(A1, A2, A3, A4, A5)>
        static ReturnType funcStub(void*, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) { return (*Method)(a1, a2, a3, a4, a5); }
        /** Method stub caller */
        template <class T, ReturnType (T::*Method)(A1, A2, A3, A4, A5)>
        static ReturnType methodStub(void* obj, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) { T* p = static_cast<T*>(obj); return (p->*Method)(a1, a2, a3, a4, a5); }
        /** Const method stub caller */
        template <class T, ReturnType (T::*Method)(A1, A2, A3, A4, A5) const>
        static ReturnType constMethodStub(void* obj, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) { T const* p = static_cast<T*>(obj); return (p->*Method)(a1, a2, a3, a4, a5); }


        /** Construction internal */
        Delegate(void* objPtr, StubType stubPtr, const char * linkedTo, const char * name) : Holder(objPtr, (void*)stubPtr, type(), linkedTo), name(name) {}
    };


// Starting from here, it's just copy and paste code of the above, with no interest whatsoever
//============================================================================================
#ifdef _MSC_VER
#define WriteType(X) \
        static const char* type() { static char func[ sizeof(__FUNCTION__) - 2]; if (!*func) { strcpy(func, strstr(__FUNCTION__, "<") + 1); *strstr(func, ">") = 0; } return func; }
#else
#define WriteType(X) \
        static const char* type() { static char func[ sizeof(__PRETTY_FUNCTION__) - 2]; if (!*func) { \
                Strings::FastString name(__PRETTY_FUNCTION__), base = name.fromTo("<", ">"), val; if (name.fromFirst("with ")) { \
                    name = name.fromFirst("with Return = "); val = name.splitFrom("; "); if (!val) val = name.splitFrom("]"); base.findAndReplace("Return(", val + "("); \
                    while(name) { const Strings::FastString & key = name.splitFrom(" = "); val = name.splitFrom("; "); if (!val) val = name.splitFrom("]"); base.findAndReplace(key, val); }} \
                    memcpy(func, (const char*)base, base.getLength()+1); } return func; }
#endif

#define WriteDelegateCode(TYPENAMES, SIG, ARGS, ARGSCALL, ARGSDECL, ARGSASSIGN, COMMA, COLUMN, INVOKE, NB) \
    template <typename Return COMMA TYPENAMES> \
    struct Delegate<Return (SIG)> : public Holder \
    { \
        typedef Return ReturnType; typedef ReturnType (*SignatureType)(SIG); \
        struct ArgType : public Arguments { FilterRet(Return) ret; ARGSDECL ArgType(ARGS) : Arguments(NB), ret(GetDefaultRet(Return)) COMMA ARGSASSIGN {} Arguments * Clone () { return new ArgType(ARGSCALL); } }; \
    private: \
        void badBoolType() {} typedef void (Delegate::*badBoolPtr)(); typedef ReturnType (*StubType)(void * COMMA SIG); const char * name; \
        static Delegate Stub(void* object, StubType stubPtr, const char * linkedTo = 0, const char * name = 0) { return Delegate(object, stubPtr, linkedTo, name); } \
        template <ReturnType (*Method)(SIG)> static ReturnType funcStub(void* COMMA ARGS) { return (*Method)(ARGSCALL); } \
        template <class T, ReturnType (T::*Method)(SIG)> static ReturnType methodStub(void* obj COMMA ARGS) { T* p = static_cast<T*>(obj); return (p->*Method)(ARGSCALL); } \
        template <class T, ReturnType (T::*Method)(SIG) const> static ReturnType constMethodStub(void* obj COMMA ARGS) { T const* p = static_cast<T*>(obj); return (p->*Method)(ARGSCALL); } \
        Delegate(void* objPtr, StubType stubPtr, const char * linkedTo, const char * name) : Holder(objPtr, (void*)stubPtr, type(), linkedTo), name(name) {} \
    public: \
        template <ReturnType (*Func)(SIG)> static Delegate From() { return Stub(0, &funcStub<Func>); } \
        template <class T, ReturnType (T::*Func)(SIG)> static Delegate From(T* obj) { return Stub(obj, &methodStub<T, Func>); } \
        template <class T, ReturnType (T::*Func)(SIG) const> static Delegate FromConst(T const* obj) { return Stub(const_cast<T*>(obj), &constMethodStub<T, Func>); } \
        template <ReturnType (*Func)(SIG)> static Delegate FromEx(const char * funcName) { return Stub(0, &funcStub<Func>, funcName); } \
        template <class T, ReturnType (T::*Func)(SIG)> static Delegate FromEx(T* obj, const char * methodName) { return Stub(obj, &methodStub<T, Func>, methodName); } \
        template <class T, ReturnType (T::*Func)(SIG) const> static Delegate FromConstEx(T const* obj, const char * methodName) { return Stub(const_cast<T*>(obj), &constMethodStub<T, Func>, methodName); } \
        Delegate() : Holder(0, 0, type(), 0), name(0) {} \
        Delegate(const Delegate & other, const char * name) : Holder(other), name(name) {} \
        static inline Delegate & delegate() { static Delegate t; return t; } \
        inline ReturnType operator() (ARGS) const { return funcPtr ? (*(StubType)funcPtr)(objPtr COMMA ARGSCALL) : GetDefaultTrueRet(ReturnType); } \
        inline void invoke(ArgType & args) const { if (!funcPtr) return; VoidObjRet<Return>::template perform<SIG COMMA StubType>(args.ret, objPtr, (StubType)funcPtr COMMA INVOKE); } \
        void invokeVirt(Arguments * args) const { ArgType * _args = dynamic_cast<ArgType *>(args); if (_args) invoke(*_args); } \
        WriteType(X) const char * getMemberName() const { return name; } \
        bool operator ! () const { return funcPtr != 0; } \
        inline operator badBoolPtr() const { return !static_cast<Delegate const&>( *this ) ? 0 : &Delegate::badBoolType; } \
    }

#define TYPENAMES typename A1, typename A2, typename A3, typename A4
#define SIG A1, A2, A3, A4
#define ARGS A1 a1, A2 a2, A3 a3, A4 a4
#define ARGSCALL a1, a2, a3, a4
#define ARGSDECL A1 a1; A2 a2; A3 a3; A4 a4;
#define ARGSASSIGN a1(a1), a2(a2), a3(a3), a4(a4)
#define INVOKE args.a1, args.a2, args.a3, args.a4
#define COMMA ,
#define COLUMN :
WriteDelegateCode(TYPENAMES, SIG, ARGS, ARGSCALL, ARGSDECL, ARGSASSIGN, COMMA, COLUMN, INVOKE, 4);
#undef TYPENAMES
#undef SIG
#undef ARGS
#undef ARGSCALL
#undef ARGSDECL
#undef ARGSASSIGN
#undef INVOKE

#define TYPENAMES typename A1, typename A2, typename A3
#define SIG A1, A2, A3
#define ARGS A1 a1, A2 a2, A3 a3
#define ARGSCALL a1, a2, a3
#define ARGSDECL A1 a1; A2 a2; A3 a3;
#define ARGSASSIGN a1(a1), a2(a2), a3(a3)
#define INVOKE args.a1, args.a2, args.a3
WriteDelegateCode(TYPENAMES, SIG, ARGS, ARGSCALL, ARGSDECL, ARGSASSIGN, COMMA, COLUMN, INVOKE, 3);

#undef TYPENAMES
#undef SIG
#undef ARGS
#undef ARGSCALL
#undef ARGSDECL
#undef ARGSASSIGN
#undef INVOKE

#define TYPENAMES typename A1, typename A2
#define SIG A1, A2
#define ARGS A1 a1, A2 a2
#define ARGSCALL a1, a2
#define ARGSDECL A1 a1; A2 a2;
#define ARGSASSIGN a1(a1), a2(a2)
#define INVOKE args.a1, args.a2
WriteDelegateCode(TYPENAMES, SIG, ARGS, ARGSCALL, ARGSDECL, ARGSASSIGN, COMMA, COLUMN, INVOKE, 2);

#undef TYPENAMES
#undef SIG
#undef ARGS
#undef ARGSCALL
#undef ARGSDECL
#undef ARGSASSIGN
#undef INVOKE

#define TYPENAMES typename A1
#define SIG A1
#define ARGS A1 a1
#define ARGSCALL a1
#define ARGSDECL A1 a1;
#define ARGSASSIGN a1(a1)
#define INVOKE args.a1
WriteDelegateCode(TYPENAMES, SIG, ARGS, ARGSCALL, ARGSDECL, ARGSASSIGN, COMMA, COLUMN, INVOKE, 1);

#undef TYPENAMES
#undef SIG
#undef ARGS
#undef ARGSCALL
#undef ARGSDECL
#undef ARGSASSIGN
#undef INVOKE
#undef COMMA
#undef COLUMN
#define XXX
WriteDelegateCode(XXX,XXX,XXX,XXX,XXX,XXX,XXX,XXX,XXX, 0);
#undef XXX
#undef WriteType
#undef WriteDelegateCode
//============== End of copy and paste code ===================================================================

#ifdef DOXYGEN
    /** Doesn't exist in reality (well, it's macro, remember?), but useful to understand how to write your object. */
    namespace Macro
    {

    /** Make a Delegate from a free function (and also capture the function name)
        This is equivalent to:
        @code
           DelegateType::FromEx<&Func>(NameOfFunc);
        @endcode
        @param DelegateType     The delegate type (likely from a typedef)
        @param Func             The function to capture (only the name of the function is required here) */
    void MakeDelFunc(X DelegateType, X Func);
    /** Make a Delegate from a method (and also capture the method name and the object's instance)
        This is equivalent to:
        @code
           DelegateType::FromEx<Class, &Class::Method>(&Obj, NameOfMethod);
        @endcode
        @param DelegateType     The delegate type (likely from a typedef)
        @param Class            The class name
        @param Method           The Method to capture (only the name of the method is required here)
        @param Obj              The instance to capture (should not be a pointer) */
    void MakeDel(X DelegateType, X Class, X Method, X Obj);
    /** Build and make a Delegate from a free function (and also capture the function name)
        This is equivalent to:
        @code
           DelegateType Name = DelegateType::FromEx<&Func>(NameOfFunc);
        @endcode
        @param DelegateType     The delegate type (likely from a typedef)
        @param Name             The name of the Delegate variable
        @param Func             The function to capture (only the name of the function is required here) */
    void BuildDelFunc(X DelegateType, X Name, X Func);
    /** Build and make a Delegate from a method (and also capture the method name and the object's intance)
        This is equivalent to:
        @code
           DelegateType Name = DelegateType::FromEx<Class, &Class::Method>(NameOfMethod);
        @endcode
        @param DelegateType     The delegate type (likely from a typedef)
        @param Name             The name of the Delegate variable
        @param Class            The class name
        @param Method           The Method to capture (only the name of the method is required here)
        @param Obj              The instance to capture (should not be a pointer) */
    void BuildDel(X DelegateType, X Name, X Class, X Method, X Obj);
    /** Declare a Delegate member in your class, and register it in the Introspection table.
        This capture the member's name, its signature and add an accessor to the member called "get_MemberName" that keep the same
        protection as the member's protection scope.
        It also create an automatic registration object so you don't need to add any other macro anywhere else in your code to register
        the delegate in the Introspection table.
        If you need to support access from multiple thread, you must have a "Threading::Lock lock" public member in your class that'll be
        detected and used automatically.

        In order to only have to declare the Delegate once (and not in the definition) we rely on numerous tricks.
        1. The first trick is to create 3 methods (one to get a reference on the Delegate, one (static) to get the name and the
           last one (static too) to get the signature as text).
        2. Then, we instantiate a template object based on pointers on these methods (unlike any other type, typename based on
           function pointer are all different even if functions have the same signature)
        3. This template object registers these pointers in the base 'Introspect<YourClass>'s static array.
           The same trick as what's used in the Variant class is used to specialize a pseudo-virtual table for YourClass to
           actually call the pointers with an instance of YourClass.
        4. Because we didn't want to have any other impact on your code anywhere else than declaration, the RegisterDel template class
           is default constructed, so it does not have your class's "this" pointer at time of registration.

        @warning To use this macro, your class must derive from "Introspect<YourClass>", else you should use usual declaration syntax.
                 The compiler will stop here if you forget the base class declaration

        This is equivalent to:
        @code
           DelegateType Name;
        @endcode

        @param DelegateType     The delegate type (likely from a typedef)
        @param Name             The member's name */
    void DeclDel(X DelegateType, X Name);
    /** Declare a property in your class and register it in the introspection table.
        A property add introspection features to your class to change it dynamically.

        This also captures the property name.
        It also create an automatic registration object so you don't need to add any other macro anywhere else in your code to register
        the property in the Introspection table.
        If you need to support access from multiple thread, you must have a "Threading::Lock lock" public member in your class that'll be
        detected and used automatically.
        @warning To use this macro, your class must derive from "Introspect<YourClass>", else you should use usual declaration syntax.
                 The compiler will stop here if you forget the base class declaration

        @warning You must assign the property in the constructor initialization list with AssignProp
        @param PropertyType     The property type
        @param Name             The member's name */
    void DeclProp(X PropertyType, X Name);
    /** Declare a property in your class, add a member with the given name, and register it in the introspection table.
        This is equivalent to:
        @code
            PropertyType Name;
        @endcode
        @param PropertyType     The property type
        @param Name             The member's name */
    void DeclPropImpl(X PropertyType, X Name);

    /** Assign a declared Delegate or Sink to a method of your class.
        This is to be used in the constructor initialization list leading to simpler code (easier to read).
        Instead of writing:
        @code
        typedef Delegate<void ()> SimpleAction;

        struct Test : public Introspect<Test>
        {
            Sink<SimpleAction> testMe;
            void methodToTest() {}

            DeclDel(SimpleAction, someDelegate);

            Test() : testMe(MakeDel(SimpleAction, Test, methodToTest, *this)),
                     someDelegate(MakeDel(SimpleAction, Test, methodToTest, *this)) {}
        };
        @endcode
        You'll write:
        @code
        typedef Delegate<void ()> SimpleAction;

        struct Test : public Introspect<Test>
        {
            Sink<SimpleAction> testMe;
            void methodToTest() {}

            DeclDel(SimpleAction, someDelegate);

            Test() : AssignDel(testMe, methodToTest),
                     AssignDel(someDelegate, methodToTest) {}
        };
        @endcode
        @param Member   The Sink or Delegate member
        @param Method   The method to bind to */
    void AssignDel(X Member, X Method);
    /** Assign a Delegate or Sink with a const method.
        @sa AssignDel for more details */
    void AssignConstDel(X Member, X Method);
    /** Assign a declared property to a member of your class.
        This is to be used in the constructor initialization list leading to simpler code (easier to read).
        Instead of writing:
        @code
        struct Test : public Introspect<Test>
        {
            String testMe;
            DeclProp(String, testMe);


            Test() : property_testMe(Property<String>::From(testMe)) {}
        };
        @endcode
        You'll write:
        @code
        struct Test : public Introspect<Test>
        {
            String testMe;
            DeclProp(String, testMe);

            Test() : AssignProp(testMe) {}
        };
        @endcode
        @param Member   The property member */
    void AssignProp(X Member);
    /** Assign a property with the given name to a member with a different name (but the same type).
        @sa AssignProp
        This is useful if your member name use a different or complex convention that you don't want to see in your property name.
        For example, if you use "m_" prefix for your members and don't want to have them replicated in the property name,
        you'll use "DeclProp(Count)" and "AssignPropEx(Count, m_Count)" for mapping the property "Count" to the member "m_Count"

        Similarly, the parseable property name in Expression limit the character set only to a-z, A-Z, _ (and 0-9 additionally starting from the 2nd character).
        This is more restrictive to what C++ allows.

        @param Property The property name, as used in DeclProp macro
        @param Member   The property member */
    void AssignPropEx(X Property, X Member);

    /** Set a Delegate or Sink to the given method.
        This one is to be used outside constructor declaration list (anywhere else).
        It's conceptually equivalent to:
        @code
            Member = Delegate::FromMethod(Method); // if such method existed
        @endcode
        Except from this, it's exactly similar to AssignDel */
    void SetDel(X Member, X Method);
    /** Set a Delegate or Sink from a const method.
        @sa SetDel for more details */
    void SetConstDel(X Member, X Method);

    /** Declare a Sink member in your class, and register it in the Introspection table.
        This capture the member's name, its Delegate's signature and add an accessor to the member called "get_MemberName" that keep the same
        protection as the member's protection scope.
        It also create an automatic registration object so you don't need to add any other macro anywhere else in your code to register
        the sink in the Introspection table.
        If you need to support access from multiple thread, you must have a "Threading::Lock lock" public member in your class that'll be
        detected and used automatically.


        @warning To use this macro, your class must derive from "Introspect<YourClass>", else you should use usual declaration syntax.
                 The compiler will stop here if you forget the base class declaration

        This is equivalent to:
        @code
           Sink<DelegateType> Name;
        @endcode

        @param DelegateType     The delegate type (likely from a typedef)
        @param Name             The member's name
        @param ...              If provided, this changes the default policy for the Sink's return type merging
        @sa AssignDel AssignConstDel SetDel SetConstDel for setting the sink to your class method */
    void DeclSink(X DelegateType, X Name, ...);
    /** Declare a Source member in your class, and register it in the Introspection table.
        This capture the member's name, its Delegate's signature and add an accessor to the member called "get_MemberName" that keep the same
        protection as the member's protection scope.
        It also create an automatic registration object so you don't need to add any other macro anywhere else in your code to register
        the source in the Introspection table.
        If you need to support access from multiple thread, you must have a "Threading::Lock lock" public member in your class that'll be
        detected and used automatically.


        @warning To use this macro, your class must derive from "Introspect<YourClass>", else you should use usual declaration syntax.
                 The compiler will stop here if you forget the base class declaration

        This is equivalent to:
        @code
           Source<DelegateType> Name;
        @endcode

        @param DelegateType     The delegate type (likely from a typedef)
        @param Name             The member's name
        @param ...              If provided, this changes the default policy for the Source's return type merging */
    void DeclSource(X DelegateType, X Name, ...);

    }

    // Default macro below so it works with later code and does not disappear from Doxygen output
  #define BuildDelFunc(DelegateType, Name, Func)           DelegateType Name = DelegateType::FromEx<&Func>(#Func)
  #define BuildDel(DelegateType, Name, Class, Method, Obj) DelegateType Name = DelegateType::FromEx<Class, &Class::Method>(&Obj, #Class "::" #Method)
  #define DeclDel(DelegateType, Name)                      DelegateType Name
  #define DeclProp(PropertyType, Name)
  #define DeclSink(DelegateType, Name, ...)                Signal::Sink<DelegateType, ##__VA_ARGS__> Name
  #define DeclSource(DelegateType, Name, ...)              Signal::Source<DelegateType, ##__VA_ARGS__> Name

#else
  #define MakeDelFunc(DelegateType, Func)           DelegateType::FromEx<&Func>(#Func)
  #define MakeDel(DelegateType, Class, Method, Obj) DelegateType::FromEx<Class, &Class::Method>(&Obj, #Class "::" #Method)

  #define AssignProp(Member)                        property_##Member(property_##Member.property().From(Member, #Member))
  #define AssignPropEx(Prop, Member)                property_##Prop(property_##Prop.property().From(Member, #Member))
  #define AssignDel(Member, Method)                 Member(Member.delegate().FromEx<ClassType, &ClassType::Method>(this, Strings::TypeToNameT<ClassType>::getTypeFromName() + "::" #Method), #Member)
  #define AssignConstDel(Member, Method)            Member(Member.delegate().FromConstEx<ClassType, &ClassType::Method>(this, Strings::TypeToNameT<ClassType>::getTypeFromName() + "::" #Method), #Member)
  #define SetDel(Member, Method)                    Member = Member.delegate().FromEx<ClassType, &ClassType::Method>(this, Strings::TypeToNameT<ClassType>::getTypeFromName() + "::" #Method)
  #define SetConstDel(Member, Method)               Member = Member.delegate().FromConstEx<ClassType, &ClassType::Method>(this, Strings::TypeToNameT<ClassType>::getTypeFromName() + "::" #Method)

  #define BuildDelFunc(DelegateType, Name, Func)           DelegateType Name = DelegateType::FromEx<&Func>(#Func)
  #define BuildDel(DelegateType, Name, Class, Method, Obj) DelegateType Name = DelegateType::FromEx<Class, &Class::Method>(&Obj, #Class "::" #Method)
  #define CONCAT3(X, Y, Z) X ## Y ## Z
  #define DeclDel(DelegateType, Name) \
            DelegateType Name; \
            inline DelegateType & get_##Name () { return Name; } \
            inline static const char * CONCAT3(get_,Name,_name)() { return #Name; } \
            inline static const char * CONCAT3(get_,Name,_sig)() { return DelegateType::type(); } \
            Signal::RegisterDel<DelegateType, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)> _regDel_##Name ; \
            friend struct Signal::RegisterDel<DelegateType, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>; \
            friend struct VirtualTableImpl<DelegateType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>
  #define DeclProp(PropertyType, Name) \
            Signal::Property<PropertyType> property_##Name; \
            inline Signal::Property<PropertyType> & get_prop_##Name () { return property_##Name; } \
            inline static const char * CONCAT3(get_,Name,_name)() { return #Name; } \
            inline static const char * CONCAT3(get_,Name,_type)() { static Strings::FastString type = Strings::getTypeName((PropertyType*)0); return type; } \
            Signal::RegisterProp<Signal::Property<PropertyType>, ClassType, &ClassType::get_prop_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_type)> _regProp_##Name ; \
            friend struct Signal::RegisterProp<Signal::Property<PropertyType>, ClassType, &ClassType::get_prop_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_type)>; \
            friend struct VirtualTableImpl< Signal::Property<PropertyType>, &ClassType::get_prop_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_type)>
  #define DeclPropImpl(PropertyType, Name) \
            PropertyType Name; \
            DeclProp(PropertyType, Name)

  #define DeclSink(DelegateType, Name, ...) \
            inline static const char * CONCAT3(get_,Name,_name)() { return #Name; } \
            Signal::NamedSink<DelegateType, &CONCAT3(get_,Name,_name), ##__VA_ARGS__> Name; \
            inline Signal::Sink<DelegateType, ##__VA_ARGS__> & get_##Name () { return Name; } \
            inline static const char * CONCAT3(get_,Name,_sig)() { return DelegateType::type(); } \
            Signal::RegisterSink<Signal::Sink<DelegateType, ##__VA_ARGS__>, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)> _regSink_##Name ; \
            friend struct Signal::RegisterSink<Signal::Sink<DelegateType, ##__VA_ARGS__>, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>; \
            friend struct VirtualTableSinkImpl<Signal::Sink<DelegateType, ##__VA_ARGS__>, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>
  #define DeclSource(DelegateType, Name, ...) \
            inline static const char * CONCAT3(get_,Name,_name)() { return #Name; } \
            Signal::NamedSource<DelegateType, &CONCAT3(get_,Name,_name), ##__VA_ARGS__> Name; \
            inline Signal::Source<DelegateType, ##__VA_ARGS__> & get_##Name () { return Name; } \
            inline static const char * CONCAT3(get_,Name,_sig)() { return DelegateType::type(); } \
            Signal::RegisterSource<Signal::Source<DelegateType, ##__VA_ARGS__>, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)> _regSource_##Name ; \
            friend struct Signal::RegisterSource<Signal::Source<DelegateType, ##__VA_ARGS__>, ClassType, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>; \
            friend struct VirtualTableSourceImpl<Signal::Source<DelegateType, ##__VA_ARGS__>, &ClassType::get_##Name , &CONCAT3(get_,Name,_name), &CONCAT3(get_,Name,_sig)>

  // Those are mapped in Doxygen but not for us
  #define SIGDOC
  #define SINKDOC
  #define ENDDOC
#endif

    namespace Private
    {
        // Check if the given template parameter has an accessible "Threading::FastLock lock" member
        template <class T>
        struct HasLock
        {
            typedef char Yes; typedef long No;
            template <typename C, C> struct Check;

            struct Fallback { int lock; };
            struct Derived : T, Fallback {};

            // This would SFINAE because if C really has `lock`, it would be ambiguous
            template <typename C>
            static No & Test(Check<int Fallback::*, &C::lock> *);
            // No ambiguity here
            template <typename C> static Yes & Test(...);
            enum { Result = sizeof(Test<Derived>(0)) == sizeof(Yes) };
        };

        template <bool val> struct B2T{};

        // If compiler breaks here, the "lock" member for your object should be of type "Threading::FastLock" or "Threading::Lock"
        template <typename T>
        inline void Lock(T & t, B2T<true> *) { t.lock.Acquire(); }
        template <typename T>
        inline void Lock(T & t, B2T<false> *) {  }

        // If compiler breaks here, the "lock" member for your object should be of type "Threading::FastLock" or "Threading::Lock"
        template <typename T>
        inline void Unlock(T & t, B2T<true> *) { t.lock.Release(); }
        template <typename T>
        inline void Unlock(T & t, B2T<false> *) {  }
    }

#if (WantAtomicClass == 1 && WantExtendedLock == 1)
    // Forward declare the Source class
    template <typename T, typename Policy> class Source;
    // Forward declare the Sink class too
    template <typename T, typename Policy> class Sink;
    // Forward declare the SourceBase class
    struct SourceBase;
    // Forward declare the SinkBase class
    struct SinkBase;


    /** Generic Sink interface base class.
        The methods declared here are accessible via introspection */
    struct SinkBase
    {
        /** Connect this sink to the given source */
        virtual bool connectTo(SourceBase *) = 0;
        /** Disconnect this sink from the given source */
        virtual bool disconnectFrom(SourceBase *) = 0;
        /** Get the underlying delegate */
        virtual Holder & getDelegate() = 0;
        /** Get the signature for this base */
        virtual const char * getType() const = 0;
        /** Get the name of the member this base (if provided) */
        virtual const char * getName() const = 0;


        /** Dump the sources connected to this sink */
        virtual Strings::FastString dumpConnectedSources(const Strings::FastString & join = ", ") const = 0;

        virtual ~SinkBase() {}
    };

    /** Generic Source interface base class */
    struct SourceBase
    {
        /** Connect this source to the given sink */
        virtual bool connectTo(SinkBase *) = 0;
        /** Disconnect this source from the given sink */
        virtual bool disconnectFrom(SinkBase *) = 0;
        /** Invoke the source with the given arguments
            @return false if the argument type does not match what's expected */
        virtual bool invoke(Arguments &) = 0;
        /** Get the signature for this base */
        virtual const char * getType() const = 0;
        /** Get the name of the member this base (if provided) */
        virtual const char * getName() const = 0;

        /** Dump the sinks connected to this source */
        virtual Strings::FastString dumpConnectedSinks(const Strings::FastString & join = ", ") const = 0;

        virtual ~SourceBase() {}
    };

#endif

    /** This is the common interface all type specific Introspect are implementing. This is used to simplify dealing with generic introspection classes */
    struct IntrospectBase
    {
        /** Get the i-th delegate.
            This is equivalent of getDelegates()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual Holder * getDelegate(const size_t i) = 0;

        /** Get a delegate by name.
            This is a long (O(N*M) operation, with N the number of delegates, and M the length of the names)
            If you intend to use this often, it's better to use findDelegateByName() instead and cache the returned index.
            @param name     The delegate's member name
            @return The index in the delegates' table or the delegate table size if not found
            @warning This obviously only works if you've used the DeclDel macro to declare your delegate in your class */
        virtual Holder * getDelegateByName(const char * name) = 0;

        /** Assign the i-th delegate.
            This is equivalent of getDelegates()[i].assign(*this, d) except that it locks your object if a "lock" member exists  */
        virtual bool assignDelegate(const size_t i, const Holder & d) = 0;

        /** Invoke the delegate.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithDelegates c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeDelegate(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getDelegates()[i].invoke(*this, args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDelegate(const size_t i, Arguments & args) = 0;

        /** Get the i-th property.
            This is equivalent of getProperties()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual PropertyBase * getProperty(const size_t i) = 0;

        /** Get a property by name.
            This is a long (O(N*M) operation, with N the number of properties, and M the length of the names)
            If you intend to use this often, it's better to use findPropertyByName() instead and cache the returned index.
            @param name     The property's member name
            @return The index in the properties' table or the property table size if not found
            @warning This obviously only works if you've used the DeclProp macro to declare your property in your class */
        virtual PropertyBase * getPropertyByName(const char * name) = 0;

        /** Assign the i-th property.
            This does not change the property's value, but links the property to another member.
            This operation is rarely needed.
            This is equivalent of getProperties()[i].assign(*this, d) except that it locks your object if a "lock" member exists  */
        virtual bool assignProperty(const size_t i, const PropertyBase & d) = 0;

#if (WantAtomicClass == 1 && WantExtendedLock == 1)
        /** Get the i-th sink.
            This is equivalent of getSinks()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SinkBase * getSink(const size_t i) = 0;

        /** Get a sink by name.
            This is a long (O(N*M) operation, with N the number of sinks, and M the length of the names)
            If you intend to use this often, it's better to use findSinkByName() instead and cache the returned index.
            @param name     The sink's member name
            @return The index in the sinks' table or the sink table size if not found
            @warning This obviously only works if you've used the DeclSink macro to declare your sink in your class */
        virtual SinkBase * getSinkByName(const char * name) = 0;

        /** Get the i-th source.
            This is equivalent of getSources()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SourceBase * getSource(const size_t i) = 0;

        /** Get a source by name.
            This is a long (O(N*M) operation, with N the number of sources, and M the length of the names)
            If you intend to use this often, it's better to use findSourceByName() instead and cache the returned index.
            @param name     The source's member name
            @return The index in the sources' table or the source table size if not found
            @warning This obviously only works if you've used the DeclSource macro to declare your source in your class */
        virtual SourceBase * getSourceByName(const char * name) = 0;

        /** Invoke the source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeSource(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getSources()[i].getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeSource(const size_t i, Arguments & args) = 0;

        /** Register a dynamic source on this object.
            This is per-instance (and not valid across instances).
            You can only get such source by using the dynamic source related methods of this class (getDynamicSource, invokeDynamicSource).
            @param name     The name of the (dynamic) source
            @param sig      The signature for this (dynamic) source
            @param source   A pointer on a Source that's owned */
        virtual void registerDynamicSource(const char * name, const char * sig, SourceBase * source) = 0;

        /** Get the number of registered dynamic source */
        virtual size_t getDynamicSourceCount() = 0;

        /** Get the i-th dynamic source.
            This is equivalent of getSources()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SourceBase * getDynamicSource(const size_t i) = 0;

        /** Get a dynamic source by name.
            This is a long (O(N*M) operation, with N the number of sources, and M the length of the names)
            If you intend to use this often, it's better to use findSourceByName() instead and cache the returned index.
            @param name     The source's member name
            @return The index in the sources' table or the source table size if not found
            @warning This obviously only works if you've used the DeclSource macro to declare your source in your class */
        virtual SourceBase * getDynamicSourceByName(const char * name) = 0;

        /** Invoke the dynamic source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeDynamicSource(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getDynamicSource(i).getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDynamicSource(const size_t i, Arguments & args) = 0;
        /** Invoke the dynamic source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeDynamicSource("divider", args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getDynamicSources("divider").getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDynamicSource(const char * name, Arguments & args) = 0;
#endif
        virtual ~IntrospectBase() {}
    };

    /** This class is used to hold introspection data in for the child class.
        It uses CRTP to cast to the child class.
        It has a static array table of delegates for each registered type that's runtime registered at first use.
        From the developer point of view, to exploit this feature, you need to add "public Introspect<YourClass>" to your base classes,
        then declare your delegates with DeclDel macro (and that's it, nothing more is required).

        The cost of using an Introspect base class is paid only for the first instantiation of your Introspect<YourClass> derived object.
        For the first instantiation, all Delegate's member declared with the DeclDel macro are registered in a static array.
        For the second and later instantiation, a flag is checked for complete registration so it only cost a single bool check per instance later on.

        @note    No protection is provided here for multithreaded access to the introspection features (can be in YourClass).
                 While accessing Delegate for reading, and invoking should be safe from different threads,
                 assigning the Delegate is not.
                 If you need multithread access for both features, then you must have a public "Threading::Lock lock" member in your class
                 (it's detected automatically and used accordingly).

        @warning There is no protection for multithreaded' registration (it should happen at first instantiation, but if you have multiple
                 threads creating YourClass for the first time in your program lifetime, then you need to build a dumb instance of YourClass
                 in your main thread before creating other threads). This is very unlikely to happen in practice. */
    template <class T>
    class Introspect : public IntrospectBase
    {
        /** @name Introspection static interface */
        /**@{*/

        // Type definitiaion and enumeration
    public:
        /** Get the class type */
        typedef T ClassType;

    protected:
        /** The protected constructor to avoid construction if used out of inheritance */
        Introspect() {}
        /** The operation on delegate types must follow the same interface described below */
        struct VirtualTable
        {
            // These do not depends on an running instance
            /** Get the member name for this delegate */
            const char *  (*getName)();
            /** Get the signature for this delegate */
            const char *  (*getSignature)();

            // These depends on a running instance
            /** Get the reference on the delegate */
            Holder *      (*getReference) (T& object);
            /** Assign this delegate to a new value
                @return false if the signature between delegate does not match */
            bool          (*assign)(T& object, const Holder & other);
            /** Invoke this delegate.
                You'll need to inspect the argument object to find out the result type */
            bool          (*invoke)(T& object, Arguments & args);
        };

        /** @cond Internal */
        /** The actual implementation that's specialized and written at compile time */
        template <typename Del, Del & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        struct VirtualTableImpl
        {
            static const char * getDelegateName()                           { return (*Name)(); }
            static const char * getDelegateSignature()                      { return (*Sig)(); }
            static Holder* getReference(T & object)                         { return &(object.*Method)(); }
            static bool assignDelegate(T & object, const Holder & other)    { if (strcmp((*Sig)(), other.getSignature())) return false; (object.*Method)().assign(other); return true; }
            static bool invoke(T & object, Arguments & _args)               { typename Del::ArgType * args = dynamic_cast<typename Del::ArgType *>(&_args); if (!args) return false; (object.*Method)().invoke(*args); return true; }
        };
        /** How to link the virtual table to the delegate type */
        template <typename Del, Del & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        static VirtualTable & getTable()
        {
            static VirtualTable table =
            {
                &VirtualTableImpl<Del, Method, Name, Sig>::getDelegateName,
                &VirtualTableImpl<Del, Method, Name, Sig>::getDelegateSignature,
                &VirtualTableImpl<Del, Method, Name, Sig>::getReference,
                &VirtualTableImpl<Del, Method, Name, Sig>::assignDelegate,
                &VirtualTableImpl<Del, Method, Name, Sig>::invoke,
            };
            return table;
        }
        /** @endcond */

        /** The table of delegates */
        typedef typename Container::PlainOldData<VirtualTable*>::Array Delegates;

        /** The operation on properties must follow the same interface described below */
        struct VirtualTableProperty
        {
            // These do not depends on an running instance
            /** Get the member name for this property */
            const char *  (*getName)();
            /** Get the type for this property */
            const char *  (*getType)();

            // These depends on a running instance
            /** Get the reference on the property holder */
            PropertyBase  *  (*getReference) (T& object);
            /** Assign this property to a new value
                @return false if the type between properties does not match */
            bool             (*assign)(T& object, const PropertyBase & other);
            /** Get this property. */
            void             (*get)(T& object, void * args);
            /** Set this property. */
            void             (*set)(T& object, const void * args);
            /** Set this property unsafely. */
            bool             (*setUnsafe)(T& object, const char * value);
        };
        /** @cond Internal */
        /** The actual implementation that's specialized and written at compile time */
        template <typename Property, Property & (T::*Method)(), const char * (*Name)(), const char *(*Type)()>
        struct VirtualTablePropertyImpl
        {
            static const char * getName()                           { return (*Name)(); }
            static const char * getType()                           { return (*Type)(); }
            static PropertyBase* getReference(T & object)           { return &(object.*Method)(); }
            static bool assign(T & object, const PropertyBase & o)  { if (strcmp((*Type)(), o.getType())) return false; (object.*Method)().assign(o); return true; }
            static void get(T & object, void * _args)               { (object.*Method)().getVirt(_args); }
            static void set(T & object, const void * _args)         { (object.*Method)().setVirt(_args); }
            static bool setUnsafe(T & object, const char * _args)   { return (object.*Method)().setPropertyFromTextUnsafe(_args); }
        };

        template <typename Property, Property & (T::*Method)(), const char * (*Name)(), const char *(*Type)()>
        static VirtualTableProperty & getPropertyTable()
        {
            static VirtualTableProperty table =
            {
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::getName,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::getType,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::getReference,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::assign,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::get,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::set,
                &VirtualTablePropertyImpl<Property, Method, Name, Type>::setUnsafe
            };
            return table;
        }
        /** @endcond */

        /** The table of properties */
        typedef typename Container::PlainOldData<VirtualTableProperty*>::Array Properties;


#if (WantAtomicClass == 1 && WantExtendedLock == 1)
        /** The operation on sink types must follow the same interface described below */
        struct VirtualTableSink
        {
            // These do not depends on an running instance
            /** Get the member name for this sink */
            const char *  (*getName)();
            /** Get the signature for this sink */
            const char *  (*getSignature)();

            // These depends on a running instance
            /** Get the reference on the sink */
            SinkBase *    (*getReference) (T& object);
        };
        /** The operation on source types must follow the same interface described below */
        struct VirtualTableSource
        {
            // These do not depends on an running instance
            /** Get the member name for this source */
            const char *  (*getName)();
            /** Get the signature for this source */
            const char *  (*getSignature)();

            // These depends on a running instance
            /** Get the reference on the source */
            SourceBase *    (*getReference) (T& object);
        };

        /** @cond Internal */
        /** The actual implementation that's specialized and written at compile time */
        template <typename Sink, Sink & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        struct VirtualTableSinkImpl
        {
            static const char * getSinkName()                           { return (*Name)(); }
            static const char * getSinkSignature()                      { return (*Sig)(); }
            static SinkBase* getReference(T & object)                   { return &(object.*Method)(); }
        };
        /** How to link the virtual table to the delegate type */
        template <typename Sink, Sink & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        static VirtualTableSink & getSinkTable()
        {
            static VirtualTableSink table =
            {
                &VirtualTableSinkImpl<Sink, Method, Name, Sig>::getSinkName,
                &VirtualTableSinkImpl<Sink, Method, Name, Sig>::getSinkSignature,
                &VirtualTableSinkImpl<Sink, Method, Name, Sig>::getReference,
            };
            return table;
        }


        /** The actual implementation that's specialized and written at compile time */
        template <typename Source, Source & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        struct VirtualTableSourceImpl
        {
            static const char * getSourceName()                         { return (*Name)(); }
            static const char * getSourceSignature()                    { return (*Sig)(); }
            static SourceBase * getReference(T & object)                { return &(object.*Method)(); }
        };
        /** How to link the virtual table to the delegate type */
        template <typename Source, Source & (T::*Method)(), const char * (*Name)(), const char *(*Sig)()>
        static VirtualTableSource & getSourceTable()
        {
            static VirtualTableSource table =
            {
                &VirtualTableSourceImpl<Source, Method, Name, Sig>::getSourceName,
                &VirtualTableSourceImpl<Source, Method, Name, Sig>::getSourceSignature,
                &VirtualTableSourceImpl<Source, Method, Name, Sig>::getReference,
            };
            return table;
        }
        /** A runtime created dynamic source that's owned */
        struct DynamicSource
        {
            Strings::FastString name;
            Strings::FastString signature;
            SourceBase *        source;

            DynamicSource(const char * name, const char * signature, SourceBase * source) : name(name), signature(signature), source(source) {}
            ~DynamicSource() { delete0(source); }
        };


        /** @endcond */

        /** The table of sinks */
        typedef typename Container::PlainOldData<VirtualTableSink*>::Array Sinks;
        /** The table of sinks */
        typedef typename Container::PlainOldData<VirtualTableSource*>::Array Sources;

#endif

        // Internal machinery
    private:
        /** Check if the delegate were already registered */
        static bool & registeredAlready() { static bool reg = false; return reg; }
        /** The dynamic sources list, that's per-instance and not per type unlike static sources */
        typename Container::NotConstructible<DynamicSource>::IndexList dynamicSources;


    public:
        /** Get the delegates for this type */
        static Delegates & getDelegates()
        {
            static Delegates dels;
            return dels;
        }

        /** Register a delegate.
            The idea here is to store an array of pointer to method, indexed by the signature and name
            However, since every pointer to method will be different (they all return an unknown delegate type),
            we need to have a trick here.
            The basic idea for the trick is the same as for the Variant type, that is, we are using a
            table of VirtualTable that are specialized by the compiler upon calling this templated method */
        template <typename Type, Type & (T::*Method)(), const char * (*Name)(), const char * (*Sig)()>
        static void registerDelegate()
        {
            if (registeredAlready()) return;
            Delegates & dels = getDelegates();
            if (dels.getSize() && strcmp(dels[0]->getName(), Name()) == 0)
                registeredAlready() = true;
            else dels.Append(&getTable<Type, Method, Name, Sig>());
        }

        /** Find a delegate by name.
            This is a long (O(N*M) operation, with N the number of delegates, and M the length of the names)
            @param name     The delegate's member name
            @return The index in the delegates' table or the delegate table size if not found
            @warning This obviously only works if you've used the DeclDel macro to declare your delegate in your class */
        static size_t findDelegateByName(const char * name)
        {
            Delegates & dels = getDelegates();
            if (!name) return dels.getSize();
            for (size_t i = 0; i < dels.getSize(); i++)
                if (dels[i]->getName() && strcmp(dels[i]->getName(), name) == 0) return i;
            return dels.getSize();
        }

        /** Get the properties for this type */
        static Properties & getProperties()
        {
            static Properties props;
            return props;
        }

        /** Register a property.
            The idea here is to store an array of pointer to data, indexed by the type and name
            The basic idea for the trick is the same as for the Variant type, that is, we are using a
            table of VirtualTable that are specialized by the compiler upon calling this templated method */
        template <typename Prop, Prop & (T::*Method)(), const char * (*Name)(), const char * (*Type)()>
        static void registerProperty()
        {
            if (registeredAlready()) return;
            Properties & props = getProperties();
            if (props.getSize() && strcmp(props[0]->getName(), Name()) == 0)
                registeredAlready() = true;
            else props.Append(&getPropertyTable<Prop, Method, Name, Type>());
        }

        /** Find a property by name.
            This is a long (O(N*M) operation, with N the number of properties, and M the length of the names)
            @param name     The property's member name
            @return The index in the property' table or the property table size if not found
            @warning Obviously, This works only if you've used the DeclProp macro to declare your property in your class */
        static size_t findPropertyByName(const char * name)
        {
            Properties & props = getProperties();
            if (!name) return props.getSize();
            for (size_t i = 0; i < props.getSize(); i++)
                if (props[i]->getName() && strcmp(props[i]->getName(), name) == 0) return i;
            return props.getSize();
        }


        /** Dump the declared interface for the given class as a C++-like class declaration.
            Example output is like this:
            @code
            struct MyClass
            {
                // Delegates
                void myDelegate(int, int);
                const char* otherDelegate(const char *);

                // Sources
                void mySource();
                int anotherSource(int, float);

                // Sinks
                int someSink();
            };
            @endcode

            @param className    The name of the class to use in the C++ declaration */
        static const char * dumpInterface()
        {
            static Strings::FastString IF;
            if (!IF)
            {
                IF = Strings::FastString::Print("struct %s\n{\n", (const char *)Strings::TypeToNameT<ClassType>::getTypeFromName().fromLast("::", true).Trimmed(":"));
                Properties & props = getProperties();
                if (props.getSize()) IF += "// Properties\n";
                for (size_t i = 0; i < props.getSize(); i++)
                {
                    Strings::FastString type = props[i]->getType();
                    IF += type + " " + props[i]->getName() + ";\n";
                }

                Delegates & dels = getDelegates();
                if (dels.getSize()) IF += "\n// Delegates\n";
                for (size_t i = 0; i < dels.getSize(); i++)
                {
                    Strings::FastString sig = dels[i]->getSignature();
                    IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", dels[i]->getName())) + ";\n";
                }
#if (WantAtomicClass == 1 && WantExtendedLock == 1)
                Sources & sources = getSources();
                if (sources.getSize()) IF += "\n// Sources\n";
                for (size_t i = 0; i < sources.getSize(); i++)
                {
                    Strings::FastString sig = sources[i]->getSignature();
                    IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", sources[i]->getName())) + ";\n";
                }

                Sinks & sinks = getSinks();
                if (sinks.getSize()) IF += "\n// Sinks\n";
                for (size_t i = 0; i < sinks.getSize(); i++)
                {
                    Strings::FastString sig = sinks[i]->getSignature();
                    IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", sinks[i]->getName())) + ";\n";
                }
#endif
                IF += "};";
            }
            return IF;
        }

#if (WantAtomicClass == 1 && WantExtendedLock == 1)
        /** Get the sinks for this type */
        static Sinks & getSinks()
        {
            static Sinks sinks;
            return sinks;
        }

        /** Get the sinks for this type */
        static Sources & getSources()
        {
            static Sources sources;
            return sources;
        }

        /** Register a sink.
            The idea here is to store an array of pointer to method, indexed by indexed by the signature and name
            However, since every pointer to method will be different (they all return an unknown sink type),
            we need to have a trick here.
            The basic idea for the trick is the same as for the variant type, that is, we are using a
            table of VirtualTable that are specialized by the compiler upon calling this templated method */
        template <typename Type, Type & (T::*Method)(), const char * (*Name)(), const char * (*Sig)()>
        static void registerSink()
        {
            if (registeredAlready()) return;
            Sinks & sinks = getSinks();
            if (sinks.getSize() && strcmp(sinks[0]->getName(), Name()) == 0)
                registeredAlready() = true;
            else sinks.Append(&getSinkTable<Type, Method, Name, Sig>());
        }

        /** Find a sink by name.
            This is a long (O(N*M) operation, with N the number of delegates, and M the length of the names)
            @param name     The delegate's member name
            @return The index in the sinks' table or the sink table size if not found
            @warning Obviously this works only if you've used the DeclDel macro to declare your delegate in your class */
        static size_t findSinkByName(const char * name)
        {
            Sinks & sinks = getSinks();
            if (!name) return sinks.getSize();
            for (size_t i = 0; i < sinks.getSize(); i++)
                if (sinks[i]->getName() && strcmp(sinks[i]->getName(), name) == 0) return i;
            return sinks.getSize();
        }

        /** Register a source.
            The idea here is to store an array of pointer to method, indexed by the signature and name
            However, since every pointer to method will be different (they all return an unknown source type),
            we need to have a trick here.
            The basic idea for the trick is the same as for the variant type, that is, we are using a
            table of VirtualTable that are specialized by the compiler upon calling this templated method */
        template <typename Type, Type & (T::*Method)(), const char * (*Name)(), const char * (*Sig)()>
        static void registerSource()
        {
            if (registeredAlready()) return;
            Sources & sources = getSources();
            if (sources.getSize() && strcmp(sources[0]->getName(), Name()) == 0)
                registeredAlready() = true;
            else sources.Append(&getSourceTable<Type, Method, Name, Sig>());
        }

        /** Find a source by name.
            This is a long (O(N*M) operation, with N the number of delegates, and M the length of the names)
            @param name     The delegate's member name
            @return The index in the sources' table or the source table size if not found
            @warning Obviously, this works only if you've used the DeclDel macro to declare your delegate in your class */
        static size_t findSourceByName(const char * name)
        {
            Sources & sources = getSources();
            if (!name) return sources.getSize();
            for (size_t i = 0; i < sources.getSize(); i++)
                if (sources[i]->getName() && strcmp(sources[i]->getName(), name) == 0) return i;
            return sources.getSize();
        }

#endif

        // The methods avoid many redundancy in the code
    public:
        /**@}*/
        /** @name Introspection dynamic interface */
        /**@{*/

        /** Get the i-th delegate.
            This is equivalent of getDelegates()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual Holder * getDelegate(const size_t i)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            Holder * h = getDelegates()[i]->getReference(t);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }
        /** Get a delegate by name.
            This is a long (O(N*M) operation, with N the number of delegates, and M the length of the names)
            If you intend to use this often, it's better to use findDelegateByName() instead and cache the returned index.
            @param name     The delegate's member name
            @return The index in the delegates' table or the delegate table size if not found
            @warning This obviously only works if you've used the DeclDel macro to declare your delegate in your class */
        virtual Holder * getDelegateByName(const char * name)
        {
            size_t i = findDelegateByName(name);
            if (i == getDelegates().getSize()) return 0;
            return getDelegate(i);
        }


        /** Assign the i-th delegate.
            This is equivalent of getDelegates()[i].assign(*this, d) except that it locks your object if a "lock" member exists  */
        virtual bool assignDelegate(const size_t i, const Holder & d)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            bool b = getDelegates()[i]->assign(t, d);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }
        /** Invoke the delegate.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithDelegates c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeDelegate(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getDelegates()[i].invoke(*this, args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDelegate(const size_t i, Arguments & args)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            bool b = getDelegates()[i]->invoke(t, args);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }

        /** Get the i-th property.
            This is equivalent of getProperties()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual PropertyBase * getProperty(const size_t i)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            PropertyBase * h = getProperties()[i]->getReference(t);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }
        /** Get a property by name.
            This is a long (O(N*M) operation, with N the number of properties, and M the length of the names)
            If you intend to use this often, it's better to use findPropertyByName() instead and cache the returned index.
            @param name     The property's member name
            @return The index in the properties' table or the property table size if not found
            @warning This obviously only works if you've used the DeclProp macro to declare your property in your class */
        virtual PropertyBase * getPropertyByName(const char * name)
        {
            size_t i = findPropertyByName(name);
            if (i == getProperties().getSize()) return 0;
            return getProperty(i);
        }

        /** Assign the i-th property.
            This does not change the property's value, but links the property to another member.
            This operation is rarely needed.
            This is equivalent of getProperties()[i].assign(*this, d) except that it locks your object if a "lock" member exists  */
        virtual bool assignProperty(const size_t i, const PropertyBase & d)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            bool b = getProperties()[i]->assign(t, d);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }
        /** Get the property value.
            This is based on compile time optimization.
            Example usage is like this:
            @code
            typedef Signal::Property<double> DD;
            ClassWithProperties c;

            // Invoke a delegate
            DD::Type args;
            c.getProperty(0, args);
            @endcode
            This is equivalent of getProperties()[i].get(*this, args) except that it locks your object if a "lock" member exists
            @return false if the provided type do not follow expected type */
        template <typename Type>
        bool getProperty(const size_t i, Type & args)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            VirtualTableProperty * p = getProperties()[i];
            bool b = p && Strings::getTypeName(&args) == p->getType();
            if (p) p->get(t, &args);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }
        /** Set the property value.
            This is based on compile time optimization.
            Example usage is like this:
            @code
            typedef Signal::Property<double> DD;
            ClassWithProperties c;

            // Invoke a delegate
            DD::Type args = 45;
            c.setProperty(0, args);
            @endcode
            This is equivalent of getProperties()[i].set(*this, args) except that it locks your object if a "lock" member exists
            @return false if the provided type do not follow expected type */
        template <typename Type>
        bool setProperty(const size_t i, const Type & args)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            VirtualTableProperty * p = getProperties()[i];
            bool b = p && Strings::getTypeName(&args) == p->getType();
            if (p) p->set(t, &args);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }

        /** Try to set the property given a textual representation of the content.
            This is usually not what you want, since convertion from a type to text is usually lossy.

            Example usage:
            @code
            typedef Signal::Property<double> DD;
            ClassWithProperties c;

            // Invoke a delegate
            c.setPropertyUnsafe(0, "3.14");
            @endcode
            @return false if the provided type do not follow expected type or conversion failed */
        bool setPropertyUnsafe(const size_t i, const char * value)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            VirtualTableProperty * p = getProperties()[i];
            bool b = p && p->setUnsafe(t, value);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;

        }

        /** Dump the dynamic interface.
            Unlike the other method, this one actually display what is connected where, taking into account the actual object instance.
            This is useful for debugging.

            Output is like this:
            @code
            struct MyClass
            {
                // Delegates
                void myDelegate(int, int); / * = globalFunc * /
                const char* otherDelegate(const char *);

                // Sources
                void mySource(); / * connected to: someSinkInYourClass, someOtherSinkInYourClass * /
                int anotherSource(int, float);

                // Sinks
                int someSink(); / * connected from: someSourceInYourClass * /
            };
            @endcode */
        Strings::FastString dumpDynamicInterface()
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            Strings::FastString IF;
            IF = Strings::FastString::Print("struct %s\n{\n", (const char *)Strings::TypeToNameT<ClassType>::getTypeFromName());

            Properties & props = getProperties();
            if (props.getSize()) IF += "// Properties\n";
            for (size_t i = 0; i < props.getSize(); i++)
            {
                Strings::FastString type = props[i]->getType();
                IF += type + " " + props[i]->getName() + ";";
                IF += Strings::FastString::Print(" /* = %s */", (const char*)props[i]->getReference(t)->dump());
                IF += "\n";
            }

            Delegates & dels = getDelegates();
            if (dels.getSize()) IF += "// Delegates\n";
            for (size_t i = 0; i < dels.getSize(); i++)
            {
                Strings::FastString sig = dels[i]->getSignature();
                IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", dels[i]->getName()));
                Holder * h = dels[i]->getReference(t);
                if (h->getLinkedTo()) IF += Strings::FastString::Print("; /* = %s */", h->getLinkedTo());
                IF += "\n";
            }
#if (WantAtomicClass == 1 && WantExtendedLock == 1)
            Sources & sources = getSources();
            if (sources.getSize()) IF += "\n// Sources\n";
            for (size_t i = 0; i < sources.getSize(); i++)
            {
                Strings::FastString sig = sources[i]->getSignature();
                IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", sources[i]->getName()));
                SourceBase * h = sources[i]->getReference(t);
                sig = h->dumpConnectedSinks();
                IF += "; /* connected to: " + sig + " */\n";
            }

            if (dynamicSources.getSize()) IF += "\n// Dynamic Sources\n";
            for (size_t i = 0; i < dynamicSources.getSize(); i++)
            {
                Strings::FastString sig = dynamicSources[i].signature;
                IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", (const char*)dynamicSources[i].name));
                if (dynamicSources[i].source) IF += "; /* connected to: " + dynamicSources[i].source->dumpConnectedSinks() + " */\n";
                else   IF += ";\n";
            }

            Sinks & sinks = getSinks();
            if (sinks.getSize()) IF += "\n// Sinks\n";
            for (size_t i = 0; i < sinks.getSize(); i++)
            {
                Strings::FastString sig = sinks[i]->getSignature();
                IF += sig.findAndReplace("(", Strings::FastString::Print(" %s(", sinks[i]->getName()));
                SinkBase * h = sinks[i]->getReference(t);
                sig = h->dumpConnectedSources();
                IF += "; /* connected from: " + sig + " */\n";
            }

#endif
            IF += "};";
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return IF;
        }

#if (WantAtomicClass == 1 && WantExtendedLock == 1)
        /** Get the i-th sink.
            This is equivalent of getSinks()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SinkBase * getSink(const size_t i)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            SinkBase * h = getSinks()[i]->getReference(t);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }
        /** Get a sink by name.
            This is a long (O(N*M) operation, with N the number of sinks, and M the length of the names)
            If you intend to use this often, it's better to use findSinkByName() instead and cache the returned index.
            @param name     The sink's member name
            @return The index in the sinks' table or the sink table size if not found
            @warning This obviously only works if you've used the DeclSink macro to declare your sink in your class */
        virtual SinkBase * getSinkByName(const char * name)
        {
            size_t i = findSinkByName(name);
            if (i == getSinks().getSize()) return 0;
            return getSink(i);
        }

        /** Get the i-th source.
            This is equivalent of getSources()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SourceBase * getSource(const size_t i)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            SourceBase * h = getSources()[i]->getReference(t);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }
        /** Get a source by name.
            This is a long (O(N*M) operation, with N the number of sources, and M the length of the names)
            If you intend to use this often, it's better to use findSourceByName() instead and cache the returned index.
            @param name     The source's member name
            @return The index in the sources' table or the source table size if not found
            @warning This obviously only works if you've used the DeclSource macro to declare your source in your class */
        virtual SourceBase * getSourceByName(const char * name)
        {
            size_t i = findSourceByName(name);
            if (i == getSources().getSize()) return 0;
            return getSource(i);
        }


        /** Invoke the source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeSource(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getSources()[i].getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeSource(const size_t i, Arguments & args)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            bool b = getSources()[i]->getReference(t)->invoke(args);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }

        /** Register a dynamic source on this object.
            This is per-instance (and not valid across instances).
            You can only get such source by using the dynamic source related methods of this class (getDynamicSource, invokeDynamicSource).
            @param name     The name of the (dynamic) source
            @param sig      The signature for this (dynamic) source
            @param source   A pointer on a Source that's owned */
        virtual void registerDynamicSource(const char * name, const char * sig, SourceBase * source)
        {
            for (size_t i = 0; i < dynamicSources.getSize(); i++)
                if (dynamicSources[i].name == name) return;

            dynamicSources.Append(new DynamicSource(name, sig, source));
        }

        /** Register a dynamic source on this object.
            This is per-instance (and not valid across instances).
            You can only get such source by using the dynamic source related methods of this class (getDynamicSource, invokeDynamicSource).
            @param source   A pointer on a Source that's owned */
        virtual void registerDynamicSource(SourceBase * source) { registerDynamicSource(source->getName(), source->getType(), source); }

        /** Get the number of registered dynamic source */
        virtual size_t getDynamicSourceCount() { return dynamicSources.getSize(); }

        /** Get the i-th source.
            This is equivalent of getSources()[i].getReference(*this) except that it locks your object if a "lock" member exists */
        virtual SourceBase * getDynamicSource(const size_t i)
        {
            T& t = static_cast<T&>(*this);
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            SourceBase * h = i < dynamicSources.getSize() ? dynamicSources[i].source : 0;
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }
        /** Find a dynamic source by name.
            This is a long (O(N*M) operation, with N the number of sources, and M the length of the names)
            @param name     The source's member name
            @return The index in the sources' table or the source table size if not found */
        virtual size_t findDynamicSourceByName(const char * name)
        {
            T& t = static_cast<T&>(*this);
            size_t i = 0;
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            for (; i < dynamicSources.getSize(); i++)
                if (dynamicSources[i].name == name) break;
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);

            return i;
        }

        /** Get a source by name.
            This is a long (O(N*M) operation, with N the number of sources, and M the length of the names)
            If you intend to use this often, it's better to use findDynamicSourceByName() instead and cache the returned index.
            @param name     The source's member name
            @return The index in the sources' table or the source table size if not found
            @warning This obviously only works if you've used the DeclSource macro to declare your source in your class */
        virtual SourceBase * getDynamicSourceByName(const char * name)
        {
            T& t = static_cast<T&>(*this);
            SourceBase * h = 0;
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            for (size_t i = 0; i < dynamicSources.getSize(); i++)
                if (dynamicSources[i].name == name)
                {
                    h = dynamicSources[i].source;
                    break;
                }
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return h;
        }


        /** Invoke the dynamic source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeDynamicSource(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getSources()[i].getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDynamicSource(const size_t i, Arguments & args)
        {
            T& t = static_cast<T&>(*this);
            bool b = false;
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            if (i < dynamicSources.getSize())
                b = dynamicSources[i].source->invoke(args);
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }

        /** Invoke the source.
            This is based on compile time optimization, you can't build the argument array dynamically.
            Example usage is like this:
            @code
            typedef Signal::Delegate<bool(int, int, int)> DD;
            ClassWithSources c;

            // Invoke a delegate
            DD::ArgType args(1, 2, 3);
            c.invokeSource(0, args);
            if (args.ret) printf("Success!\n");
            @endcode
            This is equivalent of getSources()[i].getReference(*this).invoke(args) except that it locks your object if a "lock" member exists
            @return false if the provided Arguments do not follow expected signature */
        virtual bool invokeDynamicSource(const char * name, Arguments & args)
        {
            T& t = static_cast<T&>(*this);
            bool b = false;
            Private::Lock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            for (size_t i = 0; i < dynamicSources.getSize(); i++)
                if (dynamicSources[i].name == name)
                {
                    b = dynamicSources[i].source->invoke(args);
                    break;
                }
            Private::Unlock(t, (Private::B2T<Private::HasLock<T>::Result>*)0);
            return b;
        }

#endif
        /** @} */
    };

    /** @cond Internal
        This is used internally for the DeclDel macro.
        This registers the delegate given as parameter into the introspection internal array */
    template <typename Type, typename Obj, Type & (Obj::*Method)(), const char * (*Name)(), const char * (*Sig)()>
    struct RegisterDel
    {
        RegisterDel()
        {
#if DEBUG_DEL == 1
            printf("Registering delegate of type '%s' for obj '%s' and member '%s' (named: '%s')\n", typeid(Type).name(), typeid(Obj).name(), typeid(Method).name(), Name());
#endif
            Introspect<Obj>::template registerDelegate<Type, Method, Name, Sig>();
        }
    };

    /** This is used internally for the DeclProp macro.
        This registers the property given as parameter into the introspection internal array */
    template <typename Prop, typename Obj, Prop & (Obj::*Method)(), const char * (*Name)(), const char * (*Type)()>
    struct RegisterProp
    {
        RegisterProp()
        {
#if DEBUG_DEL == 1
            printf("Registering property of type '%s' for obj '%s' and member '%s' (named: '%s')\n", typeid(Prop).name(), typeid(Obj).name(), typeid(Method).name(), Name());
#endif
            Introspect<Obj>::template registerProperty<Prop, Method, Name, Type>();
        }
    };

    /** This is used internally for the DeclSink macro.
        This registers the sink given as parameter into the introspection internal array */
    template <typename Type, typename Obj, Type & (Obj::*Method)(), const char * (*Name)(), const char * (*Sig)()>
    struct RegisterSink
    {
        RegisterSink()
        {
#if DEBUG_DEL == 1
            printf("Registering delegate of type '%s' for obj '%s' and member '%s' (named: '%s')\n", typeid(Type).name(), typeid(Obj).name(), typeid(Method).name(), Name());
#endif
            Introspect<Obj>::template registerSink<Type, Method, Name, Sig>();
        }
    };

    /** This is used internally for the DeclSource macro.
        This registers the source given as parameter into the introspection internal array */
    template <typename Type, typename Obj, Type & (Obj::*Method)(), const char * (*Name)(), const char * (*Sig)()>
    struct RegisterSource
    {
        RegisterSource()
        {
#if DEBUG_DEL == 1
            printf("Registering delegate of type '%s' for obj '%s' and member '%s' (named: '%s')\n", typeid(Type).name(), typeid(Obj).name(), typeid(Method).name(), Name());
#endif
            Introspect<Obj>::template registerSource<Type, Method, Name, Sig>();
        }
    };
    /** @endcond */

}

#endif
