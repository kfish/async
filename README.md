# lt::async

## Overview

`lt::async` provides a consistent interface for data-parallel operations.

 * **async**: Runs an action concurrently on each element of a vector of inputs,
returning a vector of the outputs in order
 * **async_retry**: Works with `lt::retry` to run an action concurrently on each
element of a vector of inputs, where each action is retried according to an
`lt::retry::RetryPolicy`.
 * **async_preemptible_retry**: Works with `lt::retry` to run an action concurrently
on each element of a vector of inputs, where each action is retried according to
an `lt::retry::PreemptibleRetry`.

All operations are defined by a user-supplied task function with type:
```
    std::function(tl::expected<output_type, error_type>(input_type)>
```

The parallel operations are provided by `lt::async`:
    * input: `std::vector<input_type>`
    * output: `tl::expected<std::vector<output_type>, error_type>`

If any action failed then the whole operation fails.

## Usage

### `lt::async::async<input_type, output_type, error_type>`

Concurrent, parallel evaluation of async operations. Uses `std::async` with a
launch policy of `std::launch::async` to ensure concurrent threads are used.

Run an action concurrently on each element of a vector of inputs, returning a
vector of the outputs in order. If any action failed then the whole operation
failed.


```cpp
auto input = std::vector<input_type>( ... );
auto tasks = async<input_type, output_type>();
auto f = [&](const input_type& i) {
    // Operate on one element of input i
    ...
    if (/* there was an error */) {
        return tl::unexpected(...);
    }
    // and return one element of output o
    return o;
};
auto output = tasks.map_concurrently(action, input);
//
if (output) {
    // None of the actions failed, a *output is a std::vector<output_type>
    ... do_something(output);
} else {
    // There was an error, which is stored in output.error().
    ... handle_error(output.error());
}
```

## `lt::async::async_retry<input_type, output_type, error_type>`

Works with `lt::retry` to run an action concurrently on each element of
a vector of inputs, where each action is retried independently in its own
thread. Each thread maintains its own `lt::retry::RetryStatus`, and all actions
are retried according to the same `lt::retry::RetryPolicy`.

The entire operation returns a vector of the outputs in order. If any action
failed then the whole operation failed. Note that "failure" here means that
the action encountered a non-recoverable error condition, such that there is
no point retrying.

```cpp
using std::chrono;
using lt::retry;
auto retry_policy = constantDelay(100ms) + limitRetries(10);

auto input = std::vector<input_type>( ... );
auto tasks = async_retry<input_type, output_type>(retry_policy);

auto f = [&](const input_type& i) {
    // Operate on one element of input i
    ...
    if (/* there was a non-recoverable error */) {
        return tl::unexpected(...);
    }
    // and return one element of output o
    return o;
};

auto should_retry = [&](RetryStatus status, const output_type& o) -> bool {
    // Decide whether the action should be retried. Typically we check if
    // the output is ready to return to the calling function.
    return !o.ready_for_use();
};
auto output = tasks.map_concurrently_retry(should_retry, action, input);

if (output) {
    // None of the actions failed, a *output is a std::vector<output_type>
    ... do_something(output);
} else {
    // There was an error, which is stored in output.error().
    ... handle_error(output.error());
}
```

## `lt::async::async_preemptible_retry<input_type, output_type, error_type>`

Works with `lt::retry` to run an action concurrently on each element of
a vector of inputs, where each action is retried independently in its own
thread. Each thread maintains its own `lt::retry::PreemptibleRetryStatus`, and
all actions are retried according to the same `lt::retry::PreemptibleRetry`.

The entire operation returns a vector of the outputs in order. If any action
failed then the whole operation failed. Note that "failure" here means that
the action encountered a non-recoverable error condition, such that there is
no point retrying.

```cpp
using std::chrono;
using lt::retry;
auto retry_policy_before = constantDelay(100ms) + limitRetries(100);
auto retry_policy_after = exponentialBackoff(1ms) + limitRetries(10);

auto input = std::vector<input_type>( ... );
auto tasks = async_retry<input_type, output_type>(retry_policy);

std::condition_variable cv;
std::mutex cv_mutex;

bool signal_condition = false;

auto signalled = [&signal_condition]() -> bool {
    return signal_condition;
};

auto f = [&](const input_type& i) {
    // Operate on one element of input i
    ...
    if (/* there was a non-recoverable error */) {
        return tl::unexpected(...);
    }

    if (/* we detect that ths signal condition has become true */) {
        std::lock_guard<std::mutex> lock(cv_mutex);
        signal_condition = true;
    }

    // and return one element of output o
    return o;
};

auto should_retry = [&](RetryStatus status, const output_type& o) -> bool {
    // Decide whether the action should be retried. Typically we check if
    // the output is ready to return to the calling function.
    return !o.ready_for_use();
};

auto output = tasks.map_concurrently_preemptible_retry(
    cv, cv_mutex, signalled, should_retry, action, input);

if (output) {
    // None of the actions failed, a *output is a std::vector<output_type>
    ... do_something(output);
} else {
    // There was an error, which is stored in output.error().
    ... handle_error(output.error());
}
```
