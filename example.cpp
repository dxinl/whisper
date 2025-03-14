#include "whisper.h"
#include <cstddef>
#include <iostream>
#include <string>
#include <tuple>

class Node1 : public whisper::Node<std::nullptr_t, int> {
public:
  whisper::Output<int> run(std::nullptr_t input) override {
    std::cout << "Node1::run" << std::endl;
    return whisper::Output<int>{0, "success", 1};
  }
};

class Node2 : public whisper::Node<int, std::string> {
public:
  whisper::Output<std::string> run(int input) override {
    std::cout << "Node2::run" << std::endl;
    return whisper::Output<std::string>{0, "success",
                                              std::to_string(input)};
  }
};

class Node3 : public whisper::Node<std::string, std::string> {
public:
  whisper::Output<std::string> run(std::string input) override {
    std::cout << "Node3::run" << std::endl;
    return whisper::Output<std::string>{0, "success", "Node3_" + input};
  }
};

class Node4 : public whisper::Node<std::string, int> {
public:
  whisper::Output<int> run(std::string input) override {
    std::cout << "Node4::run" << std::endl;
    return whisper::Output<int>{0, "success", 4};
  }
};

class Node5 : public whisper::Node<std::tuple<std::string, int>, std::string> {
public:
  whisper::Output<std::string>
  run(std::tuple<std::string, int> input) override {
    std::cout << "Node5::run" << std::endl;
    return whisper::Output<std::string>{
        0, "success",
        std::get<0>(input) + ";" + std::to_string(std::get<1>(input))};
  }
};

int main() {
  auto node = (new Node1())
                  ->then(new Node2())
                  ->join(new Node3(), new Node4())
                  ->then(new Node5);

  auto output = node->run(nullptr);
  std::cout << output.err_code << " " << output.err_msg << " " << (output.data.has_value() ? output.data.value() : "null") << std::endl;

  // std::cout << "Hello, from test!\n";
  return 0;
}
