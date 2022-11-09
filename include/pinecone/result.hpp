#pragma once

#include <variant>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "pinecone/domain/curl_result.hpp"

using json = nlohmann::json;

namespace pinecone
{
/**
 * @brief The possible ways in which a Pinecone API call can fail.
 * @details
 * While there are many different reasons why any one of these failure modes may occur, API users
 * are likely to handle failures depending on which of these categories the failure belongs to. For
 * example, there is likely little that a client can do about
 * `parsing_failed`, but retrying a `request_rejected` in the future could be a reasonable course of
 * action.
 */
enum class failure {
  /**
   * @brief No failure; the request was processed and succeeded as expected.
   */
  none,
  /**
   * @brief The API request was sent successfully, but the server did not respond. This
   * usually indicates a Pinecone outage or client-side network misconfiguration, but could
   * potentially also be caused by a bug in cppinecone depending on the error code.
   */
  request_rejected,
  /**
   * @brief The API request was sent successfully, but the server returned a non-200 HTTP response.
   * This usually indicates a bug within the client code, such as providing an invalid index name or
   * invalid api key.
   */
  request_failed,
  /**
   * @brief The API request was sent and processed successfully by the server, but the server's
   * response could not be parsed. This usually indicates a bug in cppinecone and should not be
   * expected during standard use.
   */
  parsing_failed
};

template <failure F>
struct failure_reason {
};

template <>
struct failure_reason<failure::request_rejected> {
  explicit constexpr failure_reason(domain::curl_result::error_type err) noexcept : _curl_err(err)
  {
  }

  [[nodiscard]] constexpr auto curl_error() const noexcept -> domain::curl_result::error_type
  {
    return _curl_err;
  }

 private:
  domain::curl_result::error_type _curl_err;
};
using request_rejected = failure_reason<failure::request_rejected>;

template <>
struct failure_reason<failure::request_failed> {
  failure_reason(int64_t code, std::string body) noexcept
      : _response_code(code), _body(std::move(body))
  {
  }

  [[nodiscard]] constexpr auto response_code() const noexcept -> int64_t { return _response_code; }
  [[nodiscard]] constexpr auto body() const noexcept -> std::string const& { return _body; }

 private:
  int64_t _response_code;
  std::string _body;
};
using request_failed = failure_reason<failure::request_failed>;

template <>
struct failure_reason<failure::parsing_failed> {
  explicit failure_reason(json::exception ex) noexcept : _ex(std::move(ex)) {}

  [[nodiscard]] constexpr auto exception() const noexcept -> json::exception const& { return _ex; }

 private:
  json::exception _ex;
};
using parsing_failed = failure_reason<failure::parsing_failed>;

// TODO: consolidate this and curl_result at least into its own namespace, possibly
// look to collapse some functionality as well.

template <typename T>
struct result {
  using error_type = std::variant<request_rejected, request_failed, parsing_failed>;
  using value_type = std::variant<T, error_type>;

  // NOLINTNEXTLINE
  result(T value) noexcept : _value(std::move(value)) {}
  // NOLINTNEXTLINE
  result(domain::curl_result::error_type err) noexcept : _value(request_rejected(err)) {}
  // NOLINTNEXTLINE
  result(int64_t code, std::string body) noexcept : _value(request_failed(code, std::move(body))) {}
  // NOLINTNEXTLINE
  result(json::exception ex) noexcept : _value(parsing_failed(std::move(ex))) {}
  // NOLINTNEXTLINE
  result(error_type err) noexcept : _value(std::move(err)) {}

  [[nodiscard]] constexpr auto failure_reason() const noexcept -> failure
  {
    switch (_value.index()) {
      case 0:
        return failure::none;
      case 1:
        return failure::request_rejected;
      case 2:
        return failure::request_failed;
      case 3:
        return failure::parsing_failed;
    }
  }

  [[nodiscard]] constexpr auto is_successful() const noexcept -> bool
  {
    return _value.index() == 0;
  }

  [[nodiscard]] constexpr auto is_failed() const noexcept -> bool { return !is_successful(); }

  template <typename U>
  [[nodiscard]] constexpr auto propagate() noexcept -> result<U>
  {
    return {std::move(std::get<error_type>(_value))};
  }

  template <typename U>
  constexpr auto and_then(std::function<result<U>(T&)> const& func) noexcept -> result<U>
  {
    if (is_failed()) {
      return propagate<U>();
    }

    return func(std::get<T>(_value));
  }

  [[nodiscard]] constexpr auto operator->() noexcept -> T* { return &std::get<T>(_value); }
  [[nodiscard]] constexpr auto operator*() noexcept -> T& { return std::get<T>(_value); }

  [[nodiscard]] constexpr auto value() noexcept -> value_type& { return _value; }

 private:
  value_type _value;
};
}  // namespace pinecone