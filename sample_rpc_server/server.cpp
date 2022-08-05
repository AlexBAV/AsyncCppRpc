#include "pch.h"
#include <shared/common.h>
#include <shared/simple_logger.h>

using namespace corsl::timer;

corsl::cancellation_source global_cancel;

struct simple_sum_function_object
{
	template<class T>
	corsl::future<T> operator()(const T &a, const T &b) const
	{
		// Simulate hard work
		co_await 1s;

		co_return a + b;
	}
};

class server
{
	using transport_t = transports::tcp::tcp_transport;
	using connection_t = connection<transport_t, server_of<CalculatorService>>;

	connection_t connection;

	//
	corsl::future<int> array_sum(const std::vector<int> &values);
	corsl::future<std::variant<int, std::string, error_t>> universal_add(std::variant<int, std::string> a, std::variant<int, std::string> b);

public:
	server(transport_t &&transport)
	{
		connection.set_implementation({
			// Illustrate the use of a function object
			.simple_sum = simple_sum_function_object{},
			// Illustrate the use of bind_front
			.array_sum = std::bind_front(&server::array_sum, this),
			.string_concatenate = simple_sum_function_object{},
			.universal_add = std::bind_front(&server::universal_add, this),
			// Illustrate the use of lambda. This will also work with async methods
			.send_telemetry_event = [](const telemetry_info &tm)
			{
				log(std::format("Client send telemetry event \"{}\"\n  type = {}\n  success = {}\n  occurred at {}\n"sv, tm.event,(int)tm.type, tm.success, tm.time));
			}
			});

		connection.on_error([this]([[maybe_unused]] HRESULT error_code)
			{
				log("Client disconnected. Server instance deleted.\n"sv);
				delete this;
			});

		connection.start(std::move(transport));
	}
};

corsl::future<int> server::array_sum(const std::vector<int> &values)
{
	// simulate hard work
	co_await 2s;

	co_return std::reduce(values.begin(), values.end());
}

corsl::future<std::variant<int, std::string, error_t>> server::universal_add(std::variant<int, std::string> a, std::variant<int, std::string> b)
{
	// simulate hard work
	co_await 3s;

	if (a.index() != b.index())
		co_return error_t
	{
		.error_description = "Incompatible argument types",
		.code = error_code::incompatible_types
	};
	else if (a.index() == 0)
		co_return std::get<0>(a) + std::get<0>(b);
	else
		co_return std::get<1>(a) + std::get<1>(b);
}

corsl::future<> start_server()
{
	corsl::cancellation_token token{ co_await global_cancel };

	transports::tcp::tcp_listener listener;
	co_await listener.create_server({
		.address = L"localhost"s,
		.port = 7776
		});

	while (!token.is_cancelled())
	{
		auto transport = co_await listener.wait_client(global_cancel);
		log("Client connected. Creating server instance.\n"sv);
		std::make_unique<server>(std::move(transport)).release();
	}
}

int main()
{
	log("Server started.\n"sv);
	SetConsoleCtrlHandler([](DWORD ev) -> BOOL
		{
			switch (ev)
			{
			case CTRL_C_EVENT:
			case CTRL_BREAK_EVENT:
				global_cancel.cancel();
				return true;
			default:
				return false;
			}
		}, true);

	start_server().wait();
	log("Server stopped.\n"sv);
}
