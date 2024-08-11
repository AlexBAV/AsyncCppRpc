# AsyncCppRpc
Light-weight asynchronous transport-agnostic header-only C++ RPC library.

## Highlights

* No "duck typing": C++ compiler checks RPC method names and parameter types both on client and server side.
* No macros! (Well, a single macro is still required).
* Transport-agnostic: RPC logic is completely separated from the underlying transport.
* Full duplex connection: a client makes an asynchronous call to the server and receives an answer.
* Connected parties may both act as server and client at the same time, that is, it is possible to make RPC calls in both directions.
* Allows multiple requests to be issued on the connection at the same time.
* Supports "request-response" asynchronous model and "fire-and-forget" notification model.
* Supports extendable RPC interfaces: allows to add new RPC methods to existing interfaces without the need to recompile clients.
* Propagates exceptions back to callers.
* Has built-in serialization and marshalling support for most of the native and STL types and provides a simple extensibility protocol for non-supported or custom types.

## Motivating Example

The following code snippet illustrates how to define an RPC interface:

```C++
#include <crpc/connection.h>

// Parameters may be aggregate structs
struct param_info
{
    std::string description;
    std::string additional_information;
};

// The RPC interface declaration
struct MyRpcInterface
{
    // Async RPC methods
    crpc::method<corsl::future<std::string>()> get_server_name;
    crpc::method<corsl::future<void>(const std::string &name)> set_server_name;
    crpc::method<corsl::future<int>(int a, int b)> add_numbers;

    // "Fire and forget" or void RPC methods
    crpc::method<void(const std::string &name, std::pair<int, bool> value1, 
        std::variant<int, std::optional<param_info>> value2)> notify;
};

// The following line is the only macro required by the library:
BOOST_DESCRIBE_STRUCT(MyRpcInterface, (), (get_server_name, set_server_name, add_numbers, notify));
```

### Server Implementation

Then, the following code snippet defines a server:

```C++
template<crpc::concepts::transport Transport>   // see below
corsl::future<> start_server(Transport &&transport)
{
    crpc::connection<Transport, crpc::server_of<MyRpcInterface>> connection;
    connection.set_implementation({
        .get_server_name = []() -> corsl::future<std::string>
        {
            co_return "Foo server"s;
        },
        .set_server_name = [](const std::string &name) -> corsl::future<>
        {
            // ...
            co_return;
        },
        .add_numbers = [](int a, int b) -> corsl::future<int>
        {
            co_return a + b;
        },
        .notify = [](const std::string &name, std::pair<int, bool> value1, 
            std::variant<int, std::optional<param_info>> value2)
        {

        }
    });

    corsl::promise<> server_stopped;

    connection.on_error([&](HRESULT error_code, crpc::captured_on captured_on)
    {
        // A transport has generated an unrecoverable error (including client disconnection), 
        // stop server
        server_stopped.set_async();
    });
    connection.start(std::move(transport));
    co_await server_stopped.get_future();
}
```

The `start_server` function accepts a connected transport object, provides an RPC server implementation (in this short demo, each method is implemented as a lambda, but of course any callables are supported) and starts a server instance.

When unrecoverable error occurs (this includes a client disconnection), an `on_error` callback is called and server is stopped. A server may also be stopped on demand with a call to the `stop` method. A `stop` method is also automatically called by `connection` object destructor.

### Client Implementation

A client implementation is even simpler:

```C++

template<crpc::concepts::transport Transport>
corsl::future<> client(Transport &&transport)
{
    crpc::connection<Transport, crpc::client_of<MyRpcInterface>> 
        connection{std::move(transport)};

    co_await connection.set_server_name(
        co_await connection.get_server_name() + "-modified"s);
    assert(59 == co_await connection.add_numbers(42, 17));
    connection.notify("Test"s, std::pair{ 42, true }, 
        param_info { "description"s, "additional information"s });
}
```

That's it: a client function takes a connected transport, creates a connection object and calls RPC methods **directly** on that object.

## Dependencies

This header-only library depends on the [Coroutine Support Library (corsl)](https://github.com/AlexBAV/corsl) for Windows Thread Pool-based coroutine support and on the following header-only Boost libraries: `boost.mp11`, `boost.intrusive` and `boost.describe` (version 1.79.0 or later). It also uses parts of [cista serialization library](https://github.com/felixguendling/cista).

## TOC

* [RPC Interface Declaration](#rpc-interface-declaration)
* [RPC Connection Class](#rpc-connection-class)
* [Serialization](#serialization)
* [Transports](#transports)
* [Sample](#sample)

## RPC Interface Declaration

An RPC interface is defined as a `struct`, which consists of any number of method declarations. Each method declaration must conform to the following:

```C++
crpc::method<return_type(parameters)> method_name;
```

Where `return_type` is either `void` for "fire-and-forget" methods or an awaitable type `corsl::future<T>` (`T` is any supported type, including `void`).

`parameters` is a list of RPC method's parameters. The library should be able to serialize them (more on that later). Any number from 0 to 10 parameters are supported by the library.

`method_name` is an RPC method name.

An RPC interface type must be *described* using `Boost.Describe`. Usually it means the usage of a `BOOST_DESCRIBE_STRUCT` macro:

```C++
BOOST_DESCRIBE_STRUCT(MyRpcInterface, (), (method_name1, method_name2, ... , method_nameN));
```

The library checks the following invariants and generates a compilation error if they are not satisfied:

1. `BOOST_DESCRIBE_STRUCT` has not been used on the interface.
2. Interface contains no methods.
3. Method return type is not `void` or `corsl::future<T>`.
4. One of the method parameter types cannot be serialized.

### Interface Extensibility

A server can extend published interfaces by adding new methods. It is also allowed to change the order of methods in an interface. Client re-compilation is not required in both these cases.

If server removes methods from the interface after it has been published and client calls one of the removed methods, a `corsl::hresult_error` exception with error code `E_NOTIMPL` is thrown on the client.

Changing the types, the number or order of parameters in a published method will lead to an undefined behavior.

## RPC Connection Class

The central class template `connection` is used on both sides of the RPC channel:

```C++
template<crpc::concepts::transport Transport, typename... MarshallersOrTraits>
class connection;
```

`Transport` is a transport implementation type, that must satisfy the [`transport` concept](#transports), described below.

`MarshallersOrTraits` is a list of one or two marshalling types. You get those marshalling types using the following two templates:

```C++
template<typename RpcInterface>
struct server_of;   // server-side marshaller for RpcInterface interface

template<typename RpcInterface>
struct client_of;   // client-side marshaller for RpcInterface interface
```

A single connection instantiation can be a client, server, or both client and server side of a channel. In the latter case, you can use either the single RPC interface or two different RPC interfaces for asymmetric communication.

### Optional Serializer State

You can also pass an optional *serializer state* type using the `crpc::with_serializer_state` trait:

```C++
using my_connection_t = crpc::connection<transport_t, crpc::server_of<IMyInterface>, crpc::with_serializer_state<my_custom_state>>;
```

In this case, connection type will derive from the serializer state type and its constructor will forward parameters to serializer state type's constructor. The connection object will also have a following method:

```C++
my_custom_state &get_serializer_state() noexcept;
```

Any custom `serializer_read` and `serialize_write` function (see [Custom Type Serialization](#custom-type-serialization) below) will be able to query serializer state object using the `get_state()` method from the reader or writer object passed to them.

### Starting Connection

If connection has a server-side, you must set the interface implementation before starting a connection. In this case, connection only defines a default constructor. Call `set_implementation` method with implementations of each RPC method:

```C++
crpc::connection<transport_t, crpc::server_of<MyRpcInterface>> connection;

connection.set_implementation({
    .method1 = ...,
    .method2 = ...,
    ...
});
```

A method implementation can be any callable object, like lambda, function object or an `std::bind_front` or `std::bind_back` expression.

After that, call the `start` method, passing a *connected* transport object:

```C++
connection.start(std::move(transport));
```

The method returns immediately and puts the connection in a *running* state.

If connection does not have a server side, you can either call a `start` method or provide a transport directly in connection constructor:

```C++
// First option:
crpc::connection<transport_t, crpc::client_of<MyRpcInterface>> connection;
connection.start(std::move(transport));

// Second option:
crpc::connection<transport_t, crpc::client_of<MyRpcInterface>> 
    connection{ std::move(transport) };
```

### Client-side Connection Operation

Call an RPC method directly on a client-side connection object. 

Methods that return `void` return immediately. Parameter serialization has already been completed by the time method returns and it is safe to destruct any referenced parameters. However, it is not recommended to stop or disconnect a connection immediately after calling a `void` method, because the library might need some time to complete the transfer operation (it depends on the transport implementation).

Methods that return `corsl::future<T>` complete when the server receives and processes the request and after the request result is transferred back to the client.

It is allowed to call multiple RPC methods at the same time either on different threads or on a single thread:

```C++
corsl::future<> foo(connection_t &connection)
{
    auto first_method = connection.first_method(...);
    auto second_method = connection.second_method(...);
    // do some other work while both requests are being processed
    // ...
    auto first_method_result = co_await first_method;
    auto second_method_result = co_await second_method;
}
```

Note that the server implementation must be capable of processing concurrent requests in this case.

If RPC interface consists of "fire-and-forget" notification methods only, the library provides the following optimizations:

* No read requests are issued on a client side of a connection.
* No write requests are issued on a server side of a connection.

This allows the usage of an asymmetric communication channels.

### Server-side Connection Operation

There is nothing else a server needs to do besides calling the `set_implementation` and starting a connection. When server receives a request, a provided implementation is called.

If method execution takes a long time, it should suspend early, for example, by executing the following:

```C++
co_await corsl::resume_background();
```

Failure to suspend early will prevent the server from receiving any subsequent requests. If server initiates an I/O request as part of method execution, it is OK to suspend on that I/O request.

The library guarantees that each method parameter, even passed by a reference, will have a valid lifetime until the method completes.

If implementation throws an error, it gets transported back to the caller and re-thrown on a client-side.

Note: currently, only `corsl::hresult_error` exceptions are transported.

### Stopping a connection

You can call `stop` method to stop a connection. `stop` method is automatically called by the `connection` destructor.

Make sure you correctly manage connection object lifetime.

Connection is also automatically stopped when the transport reports a read error. By convention, transport also has to report a read error when the underlying channel is disconnected.

You can get notified of this situation by providing a callback via the `on_error` connection method.

## Serialization

In order to transfer a method call over the transport and execute it on the other side, a called method parameters must be serialized on the caller's side and de-serialized on the receiver's side. 

The library has built-in serialization support for the following types.

* All standard integer and floating-point types as well as `bool`.
* `std::basic_string<...>` and `std::basic_string_view<...>`.
* `std::vector<T>`, where `T` is a supported type.
* `std::pair<T1, T2>`, where `T1` and `T2` are supported types.
* `std::tuple<T...>`, where all `T`s are supported types.
* `std::variant<T...>`, where all `T`s are supported types.
* `std::optional<T>`, where `T` is a supported type.
* `std::expected<T1, T2>`, where `T1` and `T2` are supported types.
* Any trivially-copied type.
* Any `std::ranges::range<T>`, where T is a supported type.
* Any aggregate-initialize `struct` consisting of members of supported types.
* Any type "described" using `Boost.Describe`.
* Any type that provides custom serialization support.

As you see, you can have any combination of supported types, nested to any level. For example, the following type is fully supported by the library:

```C++
std::tuple<
    std::string, 
    std::optional<
        std::variant<
            std::vector<std::string>,
            std::vector<int>,
            double
        >
    >,
    bool,
    std::pair<int, bool>
>
```

Aggregate-initialize types are also automatically supported, thanks to [cista](https://github.com/felixguendling/cista) library:

```C++
struct complex_type
{
    int a;
    bool b;
    std::string c;
};
```

### Custom Type Serialization

If your type does not belong to any of the above, you can provide custom serialization logic by defining `serialize_write` and `serialize_read` functions either inside or outside of the type.  If they are provided outside of class declaration, make sure to put them into the same namespace:

```C++
class foo
{
    int a;  // having a private member will prevent this type
            // from being automatically serializable
    std::string b;

public:
    void serialize_write(crpc::writer auto &w) const
    {
        w << a << b;
    }

    void serialize_read(crpc::reader auto &r)
    {
        r >> a >> b;
    }
};

namespace test
{
    class bar
    {
        int a;  // having a private member will prevent this type
                // from being automatically serializable

    public:
        int get_a() const { return a; }
        void set_a(int a_) { a = a_; }
    };
}

//...
namespace test
{
    inline void serialize_write(crpc::writer auto &w, const bar &b)
    {
        w << b.get_a();
    }

    inline void serialize_read(crpc::reader auto &r, bar &b)
    {
        int v;
        w >> v;
        b.set_a(v);
    }
}
```

Take the following additional notes regarding supported and unsupported serialization scenarios:

* Const references are fully supported.
* Pointers (including smart pointers) are NOT supported. This is a deliberate decision in order to avoid situations of base pointer referencing an object of a derived class.

## Transports

In a nutshell, a transport is a type that satisfies the `crpc::concepts::transport` concept:

```C++
template<typename T>
concept transport = 
    std::is_default_constructible_v<T> && 
    std::is_move_constructible_v<T> &&
    requires(T &v, const T &cv, const corsl::cancellation_source &cancel, message_t message)
{
    { v.set_cancellation_token(cancel) } -> std::same_as<void>;
    { v.read() } -> std::same_as<corsl::future<message_t>>;
    { v.write(message) } -> std::same_as<corsl::future<>>;
};
```

That is, a transport must be default constructible and must implement the following methods:

`set_cancellation_token`
:   Library calls this method to associate a cancellation source with a transport. The implementation must cancel all outstanding I/O requests when this source is cancelled.

`read`
:   Initiate a read operation on the transport. This method must either produce a `message_t` object, or throw a `corsl::hresult_error` exception on any I/O error, including disconnection. The latter is extremely important for correct operation.

`write`
:   Send a given message over the transport. The library allows calling multiple RPC methods at the same time, but ensures a `write` transport method is called sequentially. However, for this to work correctly, a transport can only complete the `write` request when it is ready to accept another one in order to avoid interleaving bytes on the stream.

Transport implementation must guarantee correct message delivery. If required, message integrity and encryption should also be implemented by a transport. If a transport is unable to deliver a message, it should throw an exception, indicating a connection loss.

### Provided Transports

Currently, the library comes with `tcp_transport`, `pipe_transport` and `copydata_transport` implementations. It also comes with a generic `dynamic_transport` type which allows a single connection object to be used with different transports at runtime.

#### `tcp_transport` Transport

This is an implementation of TCP/IP transport, compatible with Windows 8.1 or later. It uses the Windows Runtime API.

There is also a `tcp_listener` class that helps to create a listening socket. It has the following methods:

```C++
corsl::future<> create_server(const tcp_config &config);
```

Create a listening socket and bind it to a given `address` and `port`.

```C++
corsl::future<tcp_transport> wait_client(const corsl::cancellation_source &cancel);
```

Wait for a client and produce a connected `tcp_transport` object on successful connection.

`tcp_transport` class has the following method:

```C++
corsl::future<> connect(const tcp_config &config);
```

It establishes a connection to a given endpoint (referenced by `address` and `port` members of `tcp_config` structure):

```C++
// Server side
crpc::transports::tcp::tcp_listener listener;
co_await listener.create_server({ L"localhost"s, 5000 });
auto transport = co_await listener.wait_client(cancel);
// use the connected transport object
...

// Client side
crpc::transports::tcp::tcp_transport transport;
co_await transport.connect({ L"servername"s, 5000 });
// use the connected transport object
...
```

#### `pipe_transport` Transport

This is a transport implementation over named pipes. Named pipes connect endpoints both on a single computer or on different computers on networks.

In order to create a named pipe server side of a transport, call the following method:

```C++
namespace crpc::transports::pipe
{
    template<typename F = std::identity>
    inline corsl::future<pipe_transport> create_server(
        std::wstring_view pipe_name, const corsl::cancellation_source &cancel, 
        const create_server_params<F> &params = {})
}
```

Where `pipe_name` is a name of a pipe, `cancel` is a cancellation source and `params` is a set of optional pipe server parameters:

```C++
template<typename F = std::identity>
struct create_server_params
{
    const SECURITY_DESCRIPTOR *sd{};
    uint32_t def_buffer_size{ 4096 * 4096 };
    uint32_t out_buffer_size{ def_buffer_size };
    uint32_t in_buffer_size{ def_buffer_size };
    uint32_t default_timeout{};
    F on_after_wait_pending{};
    bool local_only{ false };
};
```

This function creates a named pipe server and waits for the client connection. If set, `on_after_wait_pending` callback is called after the wait is started. This allows the user to signal an event, for example.

When client connects, this function produces a connected transport object.

In order to create a named pipe client transport, use the following method:

```C++
namespace crpc::transports::pipe
{
    inline pipe_transport create_client(
        std::wstring_view server, std::wstring_view pipe_name, 
        winrt::Windows::Foundation::TimeSpan timeout, unsigned retries);
}
```

Where `server` is the name or address of a server, `pipe_name` is the name of the pipe on the server, `timeout` - is a `std::chrono`-compatible connection timeout value and `retries` is the number of connection attempts.

The function returns a connected transport object or throws an exception if error occurs.

#### `copydata_transport` Transport

This transport implementation is used to communicate with a window (by sending `WM_COPYDATA` messages to its window procedure). The target window may belong to the same or to another process. It supports both one-way and two-way communications.

For one-way communication, only server has to have a window (and all RPC methods must be "fire-and-forget" void methods). Note that it can be a special "message-only" invisible window, which can be created by any process, even non-interactive one (like Windows Service). This kind of window is created by passing a special `HWND_MESSAGE` handle as a parent window.

For two-way communications, both client and server need to have windows. Any kind of RPC method is supported in this mode.

Create an instance of `crpc::transports::copydata::copydata_transport`, and initialize it either using the constructor, or by calling the `initialize` method:

```C++
copydata_transport(HWND other_party, HWND this_party = {}, bool sync_write = false);
void initialize(HWND other_party, HWND this_party = {}, bool sync_write = false);
```

`other_party` is a window handle of a remote party and `this_party` is a window handle of a local party. This parameter is optional and if set, is validated in DEBUG builds. 

It is recommended to set `sync_write` parameter to `true` for one-way notification-only mode on the client side. This makes `copydata_transport::write` method to behave synchronously, allowing the caller to safely destroy the RPC connection object immediately after a call to an RPC method.

The window procedure must forward `WM_COPYDATA` messages to the transport using one of the following methods:

```C++
bool on_copydata(const MSG &msg) noexcept;
bool on_copydata(HWND caller, const COPYDATASTRUCT &cds) noexcept;
```

These methods return `true` if they successfully process the window message and `false` otherwise. You can use the `get_transport` connection method to get a reference to the connection's transport instance.

## Request Cancellation

Currently, the library lacks support for cancelling outstanding RPC requests from the client-side. However, if connection is broken, any outstanding requests are completed with an exception.

Request cancellation support is planned in future versions.

## Sample

This repository includes a sample implementation of an RPC server and RPC client.

Visual Studio 2022 17.9.3 has been tested.

Clone the repository, open the `AsyncCppRpc.sln` file in Visual Studio and build the solution. It will download all required dependencies through NuGet and vcpkg. Your vcpkg integration must be updated and integrated with Visual Studio for this to work.

After build, launch `sample_rpc_server.exe`. It will create a TCP listener and will wait for client connections on `localhost:7776`.

Then launch one or more instances of `sample_rpc_client.exe`. It will start a number of tests and you will see the results of those tests in both client and server console windows.
