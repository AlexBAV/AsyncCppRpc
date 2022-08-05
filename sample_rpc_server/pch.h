#pragma once

// Windows
#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <Windows.h>

// stl
#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <ranges>
#include <concepts>
#include <span>
#include <format>
#include <iostream>
#include <algorithm>
#include <numeric>

// corsl
#include <corsl/all.h>

// crpc
#include <crpc/connection.h>
#include <crpc/tcp_transport.h>

using namespace std::literals;
