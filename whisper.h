#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace whisper {

// 前置声明
template <size_t N, typename...> struct GetNthType;

// 基础模板（处理 N=0 的情况）
template <typename T, typename... Ts> struct GetNthType<0, T, Ts...> {
  using type = T;
};

// 递归模板（处理 N>0 的情况）
template <size_t N, typename T, typename... Ts> struct GetNthType<N, T, Ts...> {
  using type = typename GetNthType<N - 1, Ts...>::type;
};

enum class ErrCode {
  SUCCESS = 0,
  CONN_ERR = -1,
  PREV_ERR = -2,
  NO_VALUE = -3,
};

template <typename OUT> class Producer {};

template <typename IN> class Consumer {};

template <typename T> struct CommonOutput {
  int err_code;
  std::string err_msg;
  std::optional<T> data;
};

template <typename IN, typename C, typename OUT> class Connector;

template <typename IN, typename C, typename... OUT> class JoinConnector;

template <typename IN, typename OUT>
class Node : public Producer<CommonOutput<OUT>>, public Consumer<IN> {
private:
  template <typename... NOUT, size_t... I>
  void put(JoinConnector<IN, OUT, NOUT...> *connector,
           Node<OUT, NOUT> *...nexts, std::index_sequence<I...>) {
    ((connector->tails[I] = static_cast<Consumer<OUT> *>(nexts)), ...);
  }

public:
  virtual CommonOutput<OUT> run(IN input) = 0;

  template <typename NOUT>
  Connector<IN, OUT, NOUT> *then(Node<OUT, NOUT> *next) {
    auto *connector = new Connector<IN, OUT, NOUT>();
    connector->head = this;
    connector->tail = next;
    return connector;
  }

  template <typename... NOUT>
  JoinConnector<IN, OUT, NOUT...> *join(Node<OUT, NOUT> *...nexts) {
    auto *connector = new JoinConnector<IN, OUT, NOUT...>();
    connector->head = this;
    put<NOUT...>(connector, nexts...,
                 std::make_index_sequence<sizeof...(NOUT)>());

    return connector;
  }
};

template <typename IN, typename C, typename OUT>
class Connector : public Node<IN, OUT> {
private:
  Node<IN, C> *head;
  Node<C, OUT> *tail;

  friend class Node<IN, C>;

public:
  ~Connector() {
    if (head != nullptr) {
      delete head;
      head = nullptr;
    }
    if (tail != nullptr) {
      delete tail;
      tail = nullptr;
    }
  }

  CommonOutput<OUT> run(IN input) override {
    if (head == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "head is nullptr", nullptr};
    }
    if (tail == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "tail is nullptr", nullptr};
    }

    CommonOutput<C> output = head->run(input);
    if (output.err_code != static_cast<int>(ErrCode::SUCCESS)) {
      return {output.err_code, output.err_msg, {}};
    }

    return tail->run(output.data.value());
  }
};

template <typename IN, typename C, typename... OUT>
class JoinConnector : public Node<IN, std::tuple<OUT...>> {
private:
  friend class Node<IN, C>;

  static constexpr size_t num_outputs = sizeof...(OUT);
  Node<IN, C> *head;
  Consumer<C> *tails[num_outputs];
  std::pair<int, std::string> errors[num_outputs];

private:
  ~JoinConnector() {
    if (head != nullptr) {
      delete head;
      head = nullptr;
    }
    for (auto i = 0; i < num_outputs; i++) {
      if (tails[i] != nullptr) {
        delete tails[i];
        tails[i] = nullptr;
      }
    }
  }

  template <size_t... I>
  std::tuple<CommonOutput<OUT>...> runTails(C input,
                                            std::index_sequence<I...>) {
    return std::make_tuple(
        runTail<typename GetNthType<I, OUT...>::type>(tails[I], input, I)...);
  }

  template <typename OI>
  CommonOutput<OI> runTail(Consumer<C> *c, C input, size_t index) {
    if (index > 0 &&
        errors[index - 1].first != static_cast<int>(ErrCode::SUCCESS)) {
      auto output =
          CommonOutput<OI>{static_cast<int>(ErrCode::PREV_ERR), "", {}};
      errors[index] = {output.err_code, output.err_msg};
      return output;
    }
    if (c == nullptr) {
      auto output = CommonOutput<OI>{static_cast<int>(ErrCode::CONN_ERR),
                                     "Joined Node@" + std::to_string(index) +
                                         " is nullptr",
                                     {}};
      errors[index] = {output.err_code, output.err_msg};
      return output;
    }

    auto consumer = static_cast<Node<C, OI> *>(c);
    auto output = consumer->run(input);
    errors[index] = {output.err_code, output.err_msg};
    return output;
  }

  template <size_t... I>
  std::tuple<OUT...>
  transformCommonOutputs(std::tuple<CommonOutput<OUT>...> outputs,
                         std::index_sequence<I...>) {
    return std::make_tuple(std::get<I>(outputs).data.value()...);
  }

public:
  CommonOutput<std::tuple<OUT...>> run(IN input) override {
    if (head == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "head is nullptr", {}};
    }

    auto head_output = head->run(input);
    if (head_output.err_code != static_cast<int>(ErrCode::SUCCESS)) {
      for (size_t i = 0; i < num_outputs; i++) {
        errors[i] = {static_cast<int>(ErrCode::SUCCESS), ""};
      }
      return {head_output.err_code, head_output.err_msg, {}};
    }

    if (!std::is_same_v<C, std::nullptr_t>) {
      if (!head_output.data.has_value()) {
        for (size_t i = 0; i < num_outputs; i++) {
          errors[i] = {static_cast<int>(ErrCode::SUCCESS), ""};
        }
        return {
            static_cast<int>(ErrCode::NO_VALUE), "head output is nullptr", {}};
      }
    }

    auto outputs =
        runTails(head_output.data.value(), std::make_index_sequence<num_outputs>());
    for (size_t i = 0; i < num_outputs; i++) {
      auto &[err_code, err_msg] = errors[i];
      if (err_code != static_cast<int>(ErrCode::SUCCESS)) {
        auto output = CommonOutput<std::tuple<OUT...>>{err_code, err_msg, {}};
        for (size_t j = 0; j < num_outputs; j++) {
          errors[j] = {static_cast<int>(ErrCode::SUCCESS), ""};
        }
        return output;
      }
    }

    for (size_t j = 0; j < num_outputs; j++) {
      errors[j] = {static_cast<int>(ErrCode::SUCCESS), ""};
    }

    return {static_cast<int>(ErrCode::SUCCESS), "",
            std::make_optional(transformCommonOutputs(
                outputs, std::make_index_sequence<num_outputs>()))};
  }
};

} // namespace whisper
