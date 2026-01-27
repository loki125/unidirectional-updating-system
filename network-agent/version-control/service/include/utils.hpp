#pragma once

#include <string>
#include <cstdlib>

#define READY_PATH "ready"
#define PROCESSING_DIR "processing"

inline const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}