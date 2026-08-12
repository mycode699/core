/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * MSVC shims for POSIX bits used by kqoffice AI (time, temp files, popen).
 * Force-included into Library_kqoffice_ai on WNT. Do not include sockets here:
 * Winsock close() is not CRT _close(); use KqNetSocket for TCP.
 */

#pragma once

#include "PortableTime.hxx"

#if defined(_WIN32)

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <windows.h>

#ifndef ssize_t
using ssize_t = std::intptr_t;
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

inline int kq_mkstemp(char* tmpl)
{
    if (!tmpl || !*tmpl)
        return -1;
    std::string path(tmpl);
    if (path.rfind("/tmp/", 0) == 0 || path.rfind("/tmp\\", 0) == 0)
    {
        char dir[MAX_PATH] = {};
        const DWORD n = ::GetTempPathA(MAX_PATH, dir);
        if (n == 0 || n >= MAX_PATH)
            return -1;
        path = std::string(dir) + path.substr(5);
        if (path.size() >= 260)
            return -1;
        std::strcpy(tmpl, path.c_str());
    }
    if (_mktemp_s(tmpl, std::strlen(tmpl) + 1) != 0)
        return -1;
    return _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
}

#ifndef mkstemp
inline int mkstemp(char* tmpl) { return kq_mkstemp(tmpl); }
#endif

#ifndef close
#define close _close
#endif
#ifndef unlink
#define unlink _unlink
#endif
#ifndef write
#define write _write
#endif
#ifndef read
#define read _read
#endif
#ifndef access
#define access _access
#endif
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
