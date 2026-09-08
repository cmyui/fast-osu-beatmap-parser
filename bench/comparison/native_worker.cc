// nlohmann/json is used only by the benchmark protocol, outside timing.
#include <fosu/parser.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

size_t parse_and_count(const std::string& data, const std::string& path, bool file) {
  fosu::Parser parser;
  auto result =
      file ? parser.parse_file(path.c_str()) : parser.parse(data.data(), data.size());
  if (!result)
    throw std::runtime_error("parse failed");
  const auto count = result.value()->hit_objects.size();
  // Keep decoding observable even with whole-program optimization.
  asm volatile("" : : "g"(result.value()) : "memory");
  return count;  // Parser destruction is inside the measured call.
}

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = Json::parse(line);
    Json response;
    try {
      const std::string path = request.at("path");
      std::ifstream input(path, std::ios::binary);
      if (!input)
        throw std::runtime_error("read failed");
      const std::string data{std::istreambuf_iterator<char>(input), {}};
      const bool file = request.at("workload") == "file";
      size_t count = 0;
      auto samples = Json::array();
      for (int i = 0; i < request.at("reps").get<int>(); ++i) {
        const auto start = Clock::now();
        count = parse_and_count(data, path, file);
        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                .count());
      }
      response = {{"count", count}, {"ns", samples}};
    } catch (const std::exception& error) {
      response = {{"error", "DecodeError"}, {"detail", error.what()}};
    }
    std::cout << response.dump() << std::endl;
  }
}
