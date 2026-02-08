#pragma once

#include <string>
#include <cstdlib>
#include <functional>

#define READY_PATH "ready"
#define PROCESSING_DIR "processing"

inline const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}

inline std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}