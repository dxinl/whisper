#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace whisper {

template <size_t N, typename...>
struct GetNthType;

template <typename T, typename... Ts>
struct GetNthType<0, T, Ts...> {
  using type = T;
};

template <size_t N, typename T, typename... Ts>
struct GetNthType<N, T, Ts...> {
  using type = typename GetNthType<N - 1, Ts...>::type;
};

enum class ErrCode {
  SUCCESS = 0,
  CONN_ERR = -1,
  PREV_ERR = -2,
  NO_VALUE = -3,
};

template <typename OUT>
class Producer {};

template <typename IN>
class Consumer {};

template <typename T>
struct Output {
  int err_code;
  std::string err_msg;
  std::optional<T> data;
};

template <typename IN, typename C, typename OUT>
class ThenOperator;

template <typename IN, typename C, typename... OUT>
class JoinOperator;

template <typename IN, typename OUT>
class Node : public Producer<Output<OUT>>, public Consumer<IN> {
 private:
  template <typename... NOUT, size_t... I>
  void put(JoinOperator<IN, OUT, NOUT...>* connector,
           Node<OUT, NOUT>*... nexts,
           std::index_sequence<I...>) {
    ((connector->tails[I] = static_cast<Consumer<OUT>*>(nexts)), ...);
  }

 public:
  virtual Output<OUT> run(IN input) = 0;

  template <typename NOUT>
  ThenOperator<IN, OUT, NOUT>* then(Node<OUT, NOUT>* next) {
    auto* connector = new ThenOperator<IN, OUT, NOUT>();
    connector->head = this;
    connector->tail = next;
    return connector;
  }

  template <typename... NOUT>
  JoinOperator<IN, OUT, NOUT...>* join(Node<OUT, NOUT>*... nexts) {
    auto* connector = new JoinOperator<IN, OUT, NOUT...>();
    connector->head = this;
    put<NOUT...>(connector, nexts...,
                 std::make_index_sequence<sizeof...(NOUT)>());

    return connector;
  }
};

template <typename IN, typename C, typename OUT>
class ThenOperator : public Node<IN, OUT> {
 private:
  Node<IN, C>* head;
  Node<C, OUT>* tail;

  friend class Node<IN, C>;

 public:
  ~ThenOperator() {
    if (head != nullptr) {
      delete head;
      head = nullptr;
    }
    if (tail != nullptr) {
      delete tail;
      tail = nullptr;
    }
  }

  Output<OUT> run(IN input) override {
    if (head == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "head is nullptr", nullptr};
    }
    if (tail == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "tail is nullptr", nullptr};
    }

    Output<C> output = head->run(input);
    if (output.err_code != static_cast<int>(ErrCode::SUCCESS)) {
      return {output.err_code, output.err_msg, {}};
    }

    return tail->run(output.data.value());
  }
};

template <typename IN, typename C, typename... OUT>
class JoinOperator : public Node<IN, std::tuple<OUT...>> {
 private:
  friend class Node<IN, C>;

  static constexpr size_t num_outputs = sizeof...(OUT);
  Node<IN, C>* head;
  Consumer<C>* tails[num_outputs];
  std::pair<int, std::string> errors[num_outputs];

 private:
  ~JoinOperator() {
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
  std::tuple<Output<OUT>...> runTails(C input, std::index_sequence<I...>) {
    return std::make_tuple(
        runTail<typename GetNthType<I, OUT...>::type>(tails[I], input, I)...);
  }

  template <typename OI>
  Output<OI> runTail(Consumer<C>* c, C input, size_t index) {
    auto output = actualRunTail<OI>(c, input, index);
    errors[index] = {output.err_code, output.err_msg};
    return output;
  }

  template <typename OI>
  Output<OI> actualRunTail(Consumer<C>* c, C input, size_t index) {
    if (index > 0 &&
        errors[index - 1].first != static_cast<int>(ErrCode::SUCCESS)) {
      return {static_cast<int>(ErrCode::PREV_ERR), "", {}};
    }
    if (c == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR),
              "Joined Node@" + std::to_string(index) + " is nullptr",
              {}};
    }

    auto consumer = static_cast<Node<C, OI>*>(c);
    return consumer->run(input);
  }

  template <size_t... I>
  std::tuple<OUT...> transformOutputs(std::tuple<Output<OUT>...> outputs,
                                      std::index_sequence<I...>) {
    return std::make_tuple(transformOutput(std::get<I>(outputs))...);
  }

  template <typename T>
  T transformOutput(Output<T> output) {
    if constexpr (std::is_same_v<T, std::nullptr_t>) {
      return nullptr;
    } else {
      return output.data.value();
    }
  }

  Output<std::tuple<OUT...>> runInner(IN input) {
    if (head == nullptr) {
      return {static_cast<int>(ErrCode::CONN_ERR), "head is nullptr", {}};
    }

    auto head_output = head->run(input);
    if (head_output.err_code != static_cast<int>(ErrCode::SUCCESS)) {
      return {head_output.err_code, head_output.err_msg, {}};
    }

    if constexpr (!std::is_same_v<C, std::nullptr_t>) {
      if (!head_output.data.has_value()) {
        return {
            static_cast<int>(ErrCode::NO_VALUE), "head output is nullptr", {}};
      }
    }

    auto outputs = runTails(head_output.data.value(),
                            std::make_index_sequence<num_outputs>());
    for (size_t i = 0; i < num_outputs; i++) {
      auto& [err_code, err_msg] = errors[i];
      if (err_code != static_cast<int>(ErrCode::SUCCESS)) {
        return Output<std::tuple<OUT...>>{err_code, err_msg, {}};
      }
    }

    return {static_cast<int>(ErrCode::SUCCESS), "",
            std::make_optional(transformOutputs(
                outputs, std::make_index_sequence<num_outputs>()))};
  }

 public:
  Output<std::tuple<OUT...>> run(IN input) override {
    auto output = runInner(input);
    for (size_t i = 0; i < num_outputs; i++) {
      errors[i] = {0, ""};
    }
    return output;
  }
};

}  // namespace whisper
