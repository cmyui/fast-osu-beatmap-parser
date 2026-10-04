// nlohmann/json is used only by the benchmark protocol, outside timing.
#include <fosu/parser.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

size_t parse_and_count(fosu::Parser&      parser,
                       const std::string& data,
                       const std::string& path,
                       bool               file) {
  auto* result = file ? parser.parse_file(path.c_str()) : parser.parse(data);
  if (!result)
    throw std::runtime_error("parse failed");
  const auto count = result->hit_objects.size();
  // Keep decoding observable even with whole-program optimization.
  asm volatile("" : : "g"(result) : "memory");
  return count;
}

// With "reused", one parser serves every call; otherwise each call creates
// and destroys its own, inside the measured interval.
int main(int argc, char** argv) {
  const bool   reused = argc > 1 && std::string_view(argv[1]) == "reused";
  fosu::Parser reused_parser;
  std::string  line;
  while (std::getline(std::cin, line)) {
    const auto request = Json::parse(line);
    Json       response;
    try {
      const std::string path = request.at("path");
      std::ifstream     input(path, std::ios::binary);
      if (!input)
        throw std::runtime_error("read failed");
      const std::string data{std::istreambuf_iterator<char>(input), {}};
      const bool        file = request.at("workload") == "file";
      size_t            count = 0;
      auto              samples = Json::array();
      for (int i = 0; i < request.at("reps").get<int>(); ++i) {
        const auto start = Clock::now();
        if (reused) {
          count = parse_and_count(reused_parser, data, path, file);
        } else {
          fosu::Parser parser;
          count = parse_and_count(parser, data, path, file);
        }
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              Clock::now() - start)
                              .count());
      }
      response = {{"count", count}, {"ns", samples}};
    } catch (const std::exception& error) {
      response = {{"error", "DecodeError"}, {"detail", error.what()}};
    }
    std::cout << response.dump() << std::endl;
  }
}
