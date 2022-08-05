#include "pch.h"
#include <shared/common.h>
#include <shared/simple_logger.h>

using transport_t = transports::tcp::tcp_transport;
using connection_t = connection<transport_t, client_of<CalculatorService>>;

using namespace corsl::timer;

struct telemetry_event
{
	connection_t &connection;
	telemetry_info tm;

	telemetry_event(connection_t &connection, std::string event) noexcept :
		connection{ connection },
		tm{
			.event = std::move(event),
			.type = telemetry_type::beginning,
			.success = true,
			.time = std::chrono::system_clock::now()
	}
	{
		connection.send_telemetry_event(tm);
	}

	~telemetry_event()
	{
		tm.type = telemetry_type::end;
		tm.success = std::uncaught_exceptions() == 0;
		tm.time = std::chrono::system_clock::now();
		connection.send_telemetry_event(tm);
	}
};

corsl::future<> test1(connection_t &connection)
{
	telemetry_event e{ connection, "Test 1"s };

	log("Test 1: A simple sum of 17 and 42 is... "sv);
	log(std::format("{}\n"sv, co_await connection.simple_sum(17, 42)));
}

corsl::future<> test2(connection_t &connection)
{
	telemetry_event e{ connection, "Test 2"s };

	log("Test 2: Compute a sum of array values 17, 42, 33, -956... "sv);
	std::vector<int> values{ 17, 42, 33, -956 };
	log(std::format("{}\n"sv, co_await connection.array_sum(values)));
}

corsl::future<> test3(connection_t &connection)
{
	telemetry_event e{ connection, "Test 3"s };

	log("Test 3: A concatenation of \"Hello \" and \"World!\" is... "sv);
	log(std::format("\"{}\"\n"sv, co_await connection.string_concatenate("Hello "s, "World!"s)));
}

corsl::future<> test4(connection_t &connection)
{
	telemetry_event e{ connection, "Test 4"s };

	log("Test 4: Server provides a \"universal add\" method which is capable of computing 42 + 33 = ... "sv);
	log(std::format("{}\n"sv, std::get<0>(co_await connection.universal_add(42, 33))));
	log("        and concatenating \"Hello \" and \"World!\"..."sv);
	log(std::format("\"{}\"\n"sv, std::get<1>(co_await connection.universal_add("Hello "s, "World!"s))));
	log("        and even returning an error code for incorrect combination of 42 and \"Hello World!\"..."sv);
	auto result = co_await connection.universal_add(42, "Hello World!"s);
	assert(result.index() == 2);
	log(std::format("Error \"{}\"\n"sv, std::get<2>(result).error_description));
}

corsl::future<> start_client()
{
	log("Trying to connect to the server...\n"sv);
	try
	{
		transport_t transport;
		co_await transport.connect({
			.address = L"localhost"s,
			.port = 7776
			});
		log("Client successfully connected.\n"sv);

		connection_t connection{ std::move(transport) };
		co_await test1(connection);
		co_await test2(connection);
		co_await test3(connection);
		co_await test4(connection);

		co_await 3s;
		log("\nOur sample server is re-enterable. Illustrate that by launching all our tests concurrently!\n"sv);
		co_await corsl::when_all(test1(connection), test2(connection), test3(connection), test4(connection));
		co_await 3s;
		log("Exiting client.\n"sv);
	}
	catch (const corsl::hresult_error &e)
	{
		log(std::format(L"Error occurred: {}.\n"sv, e.message()));
	}
}


int main()
{
	start_client().wait();
}
