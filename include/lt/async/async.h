#pragma once

#include <future>
#include <vector>

#include "tl/expected.hpp"

namespace lt::async
{

// attempt_result_t
//
// Result of attempt on a single input
template <typename output_type, typename error_type=std::string>
using attempt_result_t = tl::expected<output_type, error_type>;

// aggregate_result_t
//
// Result of attempt on a vector of inputs
template <typename output_type, typename error_type=std::string>
using aggregate_result_t = tl::expected<std::vector<output_type>, error_type>;

// async_base
//
// Base class for async operations. Contains a non-asynchronous method `seq()`
// which operates sequentially in the calling thread, which is useful for testing
// the correctness of async operations.
//
// Run an action sequentially on each element of a vector of inputs, returning a
// vector of the outputs in order. If any action failed then the whole operation
// failed.
//
// auto input = std::vector<input_type>( ... );
// auto tasks = async_base<input_type, output_type>();
// auto f = [&](const input_type& i) {
//     // Operate on one element of input i
//     ...
//     if (/* there was an error */) {
//         return tl::unexpected(...);
//     }
//     // return one element of output o
//     return o;
// };
// auto output = tasks.map(action, input);
//
// if (output) {
//     // None of the actions failed, a *output is a std::vector<output_type>
//     ... do_something(output);
// } else {
//     // There was an error, which is stored in output.error().
//     ... handle_error(output.error());
// }
template <typename input_type, typename output_type, typename error_type=std::string>
class async_base
{
   public:
    aggregate_result_t<output_type, error_type> map(
        std::function<attempt_result_t<output_type, error_type>(const input_type&)> f,
        const std::vector<input_type>& input)
    {
        auto output = std::vector<output_type>();

        for (auto && i : input) {
            auto o = f(i);

            if (!o) {
                return tl::unexpected(o.error());
            }

            output.push_back(*o);
        }

        return output;
    }
};

// async
//
// Concurrent, parallel evaluation of async operations. Uses `std::async` with a
// launch policy of `std::launch::async` to ensure concurrent threads are used.
//
// Run an action concurrently on each element of a vector of inputs, returning a
// vector of the outputs in order. If any action failed then the whole operation
// failed.
//
// auto input = std::vector<input_type>( ... );
// auto tasks = async<input_type, output_type>();
// auto f = [&](const input_type& i) {
//     // Operate on one element of input i
//     ...
//     if (/* there was an error */) {
//         return tl::unexpected(...);
//     }
//     // and return one element of output o
//     return o;
// };
// auto output = tasks.map_concurrently(action, input);
//
// if (output) {
//     // None of the actions failed, a *output is a std::vector<output_type>
//     ... do_something(output);
// } else {
//     // There was an error, which is stored in output.error().
//     ... handle_error(output.error());
// }
template <typename input_type, typename output_type, typename error_type=std::string>
class async : public async_base<input_type, output_type, error_type>
{
   public:
    aggregate_result_t<output_type, error_type> map_concurrently(
        std::function<attempt_result_t<output_type, error_type>(const input_type&)> f,
        const std::vector<input_type>& input)
    {
        auto futures =
            std::vector<std::future<attempt_result_t<output_type, error_type>>>();

        for (auto && i : input) {
            futures.push_back(std::async(std::launch::async, f, i));
        }

        // Collect all the results
        auto results = std::vector<attempt_result_t<output_type, error_type>>();
        for (auto && f : futures) {
            results.push_back(f.get());
        };

        auto output = std::vector<output_type>();
        for (auto && r : results) {
            if (!r) {
                return tl::unexpected(r.error());
            }
            output.push_back(*r);
        }

        return output;
    }
};

} // namespace lt::async
