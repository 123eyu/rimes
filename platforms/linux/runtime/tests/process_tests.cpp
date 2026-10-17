#include "process.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    std::atomic<bool> cancelled{false};
    const auto echo = rimes::runtime::RunProcess({"/bin/cat"}, "中文🙂\n", cancelled, 2s);
    Check(echo.success && echo.output == "中文🙂\n", "stdin/stdout transport");
    const auto closed = rimes::runtime::RunProcess({"/bin/true"}, std::string(1024 * 1024, 'x'), cancelled, 2s);
    Check(!closed.success, "early stdin close must not crash or succeed");
    Check(!rimes::runtime::RunProcess({"/bin/cat"}, "oversized", cancelled, 2s, 3).success, "bounded output");
    const auto timeout = rimes::runtime::RunProcess({"/bin/sleep", "5"}, "", cancelled, 60ms);
    Check(timeout.timed_out && !timeout.success, "process deadline");
    std::thread stop([&] { std::this_thread::sleep_for(50ms); cancelled.store(true); });
    const auto cancelled_result = rimes::runtime::RunProcess({"/bin/sleep", "5"}, "", cancelled, 2s);
    stop.join();
    Check(cancelled_result.cancelled && !cancelled_result.success, "process cancellation");
    Check(!rimes::runtime::RunProcess({"/not-an-executable"}, "", cancelled, 2s).success, "spawn failure");
    cancelled.store(false);
    Check(!rimes::runtime::RunProcess({"/bin/sh", "-c", "printf partial; exit 2"}, "", cancelled, 2s).success, "failure cannot publish partial output");
    std::cout << "Bounded subprocess input/output, early close, timeout and cancellation passed\n";
}
