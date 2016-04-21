#include <realm.hpp>
#include "celero/Celero.h"

#include <random>

using namespace realm;

CELERO_MAIN

BASELINE_FIXED(DateTime, Insert1000, 10, 1000, 10)
{
    Table t;
}
