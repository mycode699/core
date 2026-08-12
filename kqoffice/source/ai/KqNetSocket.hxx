/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Small TCP helper used by Ollama / OpenAI-compatible adapters.
 * POSIX sockets on Unix; Winsock2 on Windows (not CRT fds).
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace kqoffice::ai
{
using KqSock = std::intptr_t;

inline bool kqNetEnsure()
{
#if defined(_WIN32)
    static bool inited = false;
    static bool ok = false;
    if (inited)
        return ok;
    WSADATA wsa{};
    ok = (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    inited = true;
    return ok;
#else
    return true;
#endif
}

inline void kqNetClose(KqSock fd)
{
    if (fd < 0)
        return;
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd));
#else
    ::close(static_cast<int>(fd));
#endif
}

inline void kqNetSetTimeout(KqSock fd, int timeoutMs)
{
#if defined(_WIN32)
    DWORD ms = timeoutMs < 0 ? 0 : static_cast<DWORD>(timeoutMs);
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&ms), sizeof(ms));
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

inline KqSock kqNetConnect(const char* host, int port, int timeoutMs)
{
    if (!host || !*host || !kqNetEnsure())
        return -1;
#if defined(_WIN32)
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return -1;
    const KqSock fd = static_cast<KqSock>(s);
#else
    const int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
        return -1;
    const KqSock fd = raw;
#endif
    kqNetSetTimeout(fd, timeoutMs);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
#if defined(_WIN32)
    if (::InetPtonA(AF_INET, host, &addr.sin_addr) != 1)
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host, nullptr, &hints, &res) != 0 || !res)
        {
            kqNetClose(fd);
            return -1;
        }
        std::memcpy(&addr.sin_addr, &reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr,
                    sizeof(in_addr));
        ::freeaddrinfo(res);
    }
#else
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        hostent* he = ::gethostbyname(host);
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list || !he->h_addr_list[0])
        {
            kqNetClose(fd);
            return -1;
        }
        std::memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
    }
#endif

#if defined(_WIN32)
    if (::connect(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
#else
    if (::connect(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
#endif
    {
        kqNetClose(fd);
        return -1;
    }
    return fd;
}

inline bool kqNetSendAll(KqSock fd, const char* buf, std::size_t len)
{
    std::size_t sent = 0;
    while (sent < len)
    {
#if defined(_WIN32)
        const int n = ::send(static_cast<SOCKET>(fd), buf + sent,
                             static_cast<int>(len - sent), 0);
#else
        const ssize_t n = ::send(static_cast<int>(fd), buf + sent, len - sent, 0);
#endif
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

inline std::string kqNetRecvBounded(KqSock fd, std::size_t maxBytes)
{
    std::string out;
    out.reserve(std::min<std::size_t>(maxBytes, std::size_t{2048}));
    char buf[4096];
    while (out.size() < maxBytes)
    {
#if defined(_WIN32)
        const int n = ::recv(static_cast<SOCKET>(fd), buf, sizeof(buf), 0);
        if (n == 0)
            break;
        if (n < 0)
        {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
                break;
            return std::string();
        }
#else
        const ssize_t n = ::recv(static_cast<int>(fd), buf, sizeof(buf), 0);
        if (n == 0)
            break;
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return std::string();
        }
#endif
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
