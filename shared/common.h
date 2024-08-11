#pragma once
// This file defines the server interface

using namespace crpc;

// Illustrate support for enumerations and enum classes
enum class telemetry_type
{
	beginning,
	end
};

// Illustrate support for aggregate structures and standard layout types
struct telemetry_info
{
	std::string event;
	telemetry_type type;
	bool success;
	std::chrono::time_point<std::chrono::system_clock> time;
};

// Illustrate custom serialization support
// Define custom serialization for a type
namespace std::chrono
{
	void serialize_write(crpc::writer auto &w, const std::chrono::time_point<std::chrono::system_clock> &time)
	{
		w << std::chrono::system_clock::to_time_t(time);
	}

	void serialize_read(crpc::reader auto &r, std::chrono::time_point<std::chrono::system_clock> &time)
	{
		time_t value;
		r >> value;
		time = std::chrono::system_clock::from_time_t(value);
	}
}

enum class error_code
{
	no_error,
	incompatible_types
};

struct error_t
{
	std::string error_description;
	error_code code;
};

struct CalculatorService
{
	method<corsl::future<int>(int a, int b)> simple_sum;
	method<corsl::future<int>(const std::vector<int> &values)> array_sum;
	method<corsl::future<std::string>(const std::string &a, const std::string &b)> string_concatenate;
	method<corsl::future<std::variant<int, std::string, error_t>>(std::variant<int, std::string> a, std::variant<int, std::string> b)> universal_add;

	// "Fire and forget" method
	method<void(const telemetry_info &tm)> send_telemetry_event;
};

BOOST_DESCRIBE_STRUCT(CalculatorService, (), (simple_sum, array_sum, string_concatenate, universal_add, send_telemetry_event));
