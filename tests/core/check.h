#pragma once
#include <cstdio>
#include <cstdlib>

inline int GFailures = 0;
#define CHECK(Cond) do { if (!(Cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #Cond); ++GFailures; } } while (0)
#define CHECK_EQ(A, B) do { auto _a = (A); auto _b = (B); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #A, #B); ++GFailures; } } while (0)
#define CONTAINS(Hay, Needle) ((Hay).find(Needle) != std::string::npos)
