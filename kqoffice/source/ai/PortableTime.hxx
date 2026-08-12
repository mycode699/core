/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Portable gmtime_r / localtime_r for MSVC (POSIX reentrant APIs).
 * Force-included into kqoffice_ai on WNT so existing call sites compile.
 */

#pragma once

#include <ctime>

#if defined(_WIN32)

#ifndef gmtime_r
inline std::tm* gmtime_r(const std::time_t* timep, std::tm* result)
{
    return (timep && result && gmtime_s(result, timep) == 0) ? result : nullptr;
}
#endif

#ifndef localtime_r
inline std::tm* localtime_r(const std::time_t* timep, std::tm* result)
{
    return (timep && result && localtime_s(result, timep) == 0) ? result : nullptr;
}
#endif

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
