#pragma once

// Minimal zero-dependency test harness for the Graph kernel conformance suite. Deliberately
// self-contained: the whole point of this test target is compiling src/Graph/** with NOTHING
// else, proving the kernel/host boundary.

#include <cstdio>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace GraphTest
{

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Case
{
    const char* Name;
    void (*Fn)();
};

inline std::vector<Case>& Registry()
{
    static std::vector<Case> registry;
    return registry;
}

struct Registrar
{
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

[[noreturn]] inline void Fail(const std::string& message, const char* file, int line)
{
    std::ostringstream os;
    os << file << "(" << line << "): " << message;
    throw Failure(os.str());
}

template <typename T>
std::string Repr(const T& value)
{
    if constexpr (requires(std::ostringstream& os, const T& v) { os << v; })
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }
    else
    {
        return "<unprintable>";
    }
}

template <typename A, typename B>
void CheckEq(const A& expected, const B& actual, const char* exprExpected, const char* exprActual,
    const char* file, int line)
{
    if (!(expected == actual))
    {
        Fail(std::string("CHECK_EQ(") + exprExpected + ", " + exprActual + ") failed\n  expected: "
                + Repr(expected) + "\n  actual:   " + Repr(actual),
            file, line);
    }
}

template <typename A, typename B>
void CheckNe(const A& lhs, const B& rhs, const char* exprL, const char* exprR, const char* file, int line)
{
    if (lhs == rhs)
    {
        Fail(std::string("CHECK_NE(") + exprL + ", " + exprR + ") failed\n  both were: " + Repr(lhs),
            file, line);
    }
}

inline int RunAll()
{
    int failed = 0;
    for (const Case& c : Registry())
    {
        try
        {
            c.Fn();
            std::printf("[PASS] %s\n", c.Name);
        }
        catch (const std::exception& e)
        {
            failed++;
            std::printf("[FAIL] %s\n  %s\n", c.Name, e.what());
        }
        catch (...)
        {
            failed++;
            std::printf("[FAIL] %s\n  unknown exception\n", c.Name);
        }
    }
    std::printf("\n%zu tests, %d failed\n", Registry().size(), failed);
    return failed ? 1 : 0;
}

} // namespace GraphTest

#define TEST(name)                                                     \
    static void name();                                                \
    static ::GraphTest::Registrar reg_##name(#name, &name);            \
    static void name()

#define CHECK(cond)                                                    \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
            ::GraphTest::Fail("CHECK(" #cond ") failed", __FILE__, __LINE__); \
    } while (0)

#define CHECK_FALSE(cond)                                              \
    do                                                                 \
    {                                                                  \
        if (cond)                                                      \
            ::GraphTest::Fail("CHECK_FALSE(" #cond ") failed", __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ(expected, actual) \
    ::GraphTest::CheckEq((expected), (actual), #expected, #actual, __FILE__, __LINE__)

#define CHECK_NE(lhs, rhs) \
    ::GraphTest::CheckNe((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

#define CHECK_THROWS(ExType, expr)                                     \
    do                                                                 \
    {                                                                  \
        bool caught_ = false;                                          \
        try                                                            \
        {                                                              \
            (void)(expr);                                              \
        }                                                              \
        catch (const ExType&)                                          \
        {                                                              \
            caught_ = true;                                            \
        }                                                              \
        catch (...)                                                    \
        {                                                              \
        }                                                              \
        if (!caught_)                                                  \
            ::GraphTest::Fail("expected " #ExType " from: " #expr, __FILE__, __LINE__); \
    } while (0)
