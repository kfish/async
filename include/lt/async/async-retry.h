#pragma once

#include "lt/async/async.h"
#include "lt/retry/retry.h"

namespace lt::async
{

// async_retry
//
// Works with lt::retry to run an action concurrently on each element of
// a vector of inputs, where each action is retried independently in its own
// thread. Each thread maintains its own lt::retry::RetryStatus, and all actions
// are retried according to the same lt::retry::RetryPolicy.
//
// The entire operation returns a vector of the outputs in order. If any action
// failed then the whole operation failed. Note that "failure" here means that
// the action encountered a non-recoverable error condition, such that there is
// no point retrying.
//
// using std::chrono;
// using lt::retry;
// auto retry_policy = constantDelay(100ms) + limitRetries(10);
//
// auto input = std::vector<input_type>( ... );
// auto tasks = async_retry<input_type, output_type>(retry_policy);
//
// auto f = [&](const input_type& i) {
//     // Operate on one element of input i
//     ...
//     if (/* there was a non-recoverable error */) {
//         return tl::unexpected(...);
//     }
//     // and return one element of output o
//     return o;
// };
//
// auto should_retry = [&](RetryStatus status, const output_type& o) -> bool {
//     // Decide whether the action should be retried. Typically we check if
//     // the output is ready to return to the calling function.
//     return !o.ready_for_use();
// };
// auto output = tasks.map_concurrently_retry(should_retry, action, input);
//
// if (output) {
//     // None of the actions failed, a *output is a std::vector<output_type>
//     ... do_something(output);
// } else {
//     // There was an error, which is stored in output.error().
//     ... handle_error(output.error());
// }
template <typename input_type, typename output_type, typename error_type=std::string>
class async_retry : public async<input_type, output_type, error_type>
{
   public:
    async_retry(const lt::retry::RetryPolicy& retry_policy)
        : retry_policy_(retry_policy)
    {
    }

    aggregate_result_t<output_type, error_type> map_concurrently_retry(
        std::function<bool(lt::retry::RetryStatus, const output_type&)> should_retry,
        std::function<attempt_result_t<output_type, error_type>(const input_type&)> f,
        const std::vector<input_type>& input)
    {
        auto inner_should_retry =
            [&should_retry](lt::retry::RetryStatus retry_status, attempt_result_t<output_type, error_type> result) -> bool {
                auto g = [&](output_type o) -> bool {
                    return should_retry(retry_status, o);
                };
                return result.map(g).value_or(false);
            };

        auto retry_f =
            [&inner_should_retry, &f, this](const input_type& i) -> attempt_result_t<output_type, error_type> {
                auto inner_action = [&i, &f](lt::retry::RetryStatus _) -> attempt_result_t<output_type, error_type> {
                    return f(i);
                };

                return retry_policy_.retry<attempt_result_t<output_type, error_type>>(inner_should_retry, inner_action);
            };

        return async<input_type, output_type, error_type>::map_concurrently(retry_f, input);
    }

   private:
    lt::retry::RetryPolicy retry_policy_;
};

// async_preemptible_retry
//
// Works with lt::retry to run an action concurrently on each element of
// a vector of inputs, where each action is retried independently in its own
// thread. Each thread maintains its own lt::retry::PreemptibleRetryStatus, and
// all actions are retried according to the same lt::retry::PreemptibleRetry.
//
// The entire operation returns a vector of the outputs in order. If any action
// failed then the whole operation failed. Note that "failure" here means that
// the action encountered a non-recoverable error condition, such that there is
// no point retrying.
//
// using std::chrono;
// using lt::retry;
// auto retry_policy_before = constantDelay(100ms) + limitRetries(100);
// auto retry_policy_after = exponentialBackoff(1ms) + limitRetries(10);
//
// auto input = std::vector<input_type>( ... );
// auto tasks = async_retry<input_type, output_type>(retry_policy);
//
// std::condition_variable cv;
// std::mutex cv_mutex;
//
// bool signal_condition = false;
//
// auto signalled = [&signal_condition]() -> bool {
//     return signal_condition;
// };
//
// auto f = [&](const input_type& i) {
//     // Operate on one element of input i
//     ...
//     if (/* there was a non-recoverable error */) {
//         return tl::unexpected(...);
//     }
//
//     if (/* we detect that ths signal condition has become true */) {
//         std::lock_guard<std::mutex> lock(cv_mutex);
//         signal_condition = true;
//     }
//
//     // and return one element of output o
//     return o;
// };
//
// auto should_retry = [&](RetryStatus status, const output_type& o) -> bool {
//     // Decide whether the action should be retried. Typically we check if
//     // the output is ready to return to the calling function.
//     return !o.ready_for_use();
// };
//
// auto output = tasks.map_concurrently_preemptible_retry(
//     cv, cv_mutex, signalled, should_retry, action, input);
//
// if (output) {
//     // None of the actions failed, a *output is a std::vector<output_type>
//     ... do_something(output);
// } else {
//     // There was an error, which is stored in output.error().
//     ... handle_error(output.error());
// }
//
template <typename input_type, typename output_type, typename error_type=std::string>
class async_preemptible_retry : public async<input_type, output_type, error_type>
{
   public:
    explicit async_preemptible_retry(const lt::retry::RetryPolicy& policy_before, const lt::retry::RetryPolicy& policy_after)
        : policy_(policy_before, policy_after)
    {
    }

    explicit async_preemptible_retry(const lt::retry::PreemptibleRetry& policy)
        : policy_(policy)
    {
    }

    aggregate_result_t<output_type, error_type> map_concurrently_preemptible_retry(
        std::condition_variable& cv,
        std::mutex& cv_mutex,
        std::function<bool()> cond,
        std::function<bool(lt::retry::PreemptibleRetryStatus, const output_type&)> should_retry,
        std::function<attempt_result_t<output_type, error_type>(const input_type&)> f,
        const std::vector<input_type>& input)
    {
        auto inner_should_retry =
            [&should_retry](lt::retry::PreemptibleRetryStatus retry_status, attempt_result_t<output_type, error_type> result) -> bool {
                auto g = [&](output_type o) -> bool {
                    return should_retry(retry_status, o);
                };
                return result.map(g).value_or(false);
            };

        auto retry_f =
            [&](const input_type& i) -> attempt_result_t<output_type, error_type> {
                auto inner_action = [&i, &f](lt::retry::PreemptibleRetryStatus _) -> attempt_result_t<output_type, error_type> {
                    return f(i);
                };

                return policy_.retry<attempt_result_t<output_type, error_type>>(
                    cv, cv_mutex, cond, inner_should_retry, inner_action);
            };

        return async<input_type, output_type, error_type>::map_concurrently(retry_f, input);
    }

   private:
    lt::retry::PreemptibleRetry policy_;
};

} // namespace lt::retry
