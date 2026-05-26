// Copyright (C) 2018-2026 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "intel_npu/utils/vcl/vcl_api.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

#include "openvino/util/file_util.hpp"
#include "openvino/util/shared_object.hpp"

namespace intel_npu {

namespace {
constexpr auto ALT_DEPENDENCY_PATH_ENV_NAME = "NPU_ALT_DEPENDENCY_PATH";
constexpr auto COMPILER_TYPE_ENV_NAME = "NPU_COMPILER_TYPE";
constexpr auto DRIVER_COMPILER_TYPE_NAME = "DRIVER";

std::string_view getActiveCompilerType() {
    if (const auto* compilerType = std::getenv(COMPILER_TYPE_ENV_NAME);
        compilerType != nullptr && *compilerType != '\0') {
        return compilerType;
    }
    return {};
}

std::filesystem::path getCompilerLibraryPath() {
    if (getActiveCompilerType() == DRIVER_COMPILER_TYPE_NAME) {
        if (const auto* customPath = std::getenv(ALT_DEPENDENCY_PATH_ENV_NAME);
            customPath != nullptr && *customPath != '\0') {
            return std::filesystem::path(customPath);
        }
    }

    return ov::util::get_ov_lib_path();
}
}  // namespace

VCLApi::VCLApi() : _logger("VCLApi", Logger::global().level()) {
    const auto compilerLibraryPath = getCompilerLibraryPath();
    const auto baseName = "openvino_intel_npu_compiler_loader";

    try {
        const auto libpath = ov::util::make_plugin_library_name(compilerLibraryPath, baseName);
        _logger.debug("Try to load: %s", ov::util::path_to_string(libpath).c_str());
        this->lib = ov::util::load_shared_object(libpath);
    } catch (const std::runtime_error& error) {
        _logger.debug("Failed to load %s: %s", baseName, error.what());
        OPENVINO_THROW(error.what());
    }

    try {
#define vcl_symbol_statement(vcl_symbol) \
    this->vcl_symbol = reinterpret_cast<decltype(&::vcl_symbol)>(ov::util::get_symbol(lib, #vcl_symbol));
        vcl_symbols_list();
#undef vcl_symbol_statement
    } catch (const std::runtime_error& error) {
        _logger.debug("Failed to get formal symbols from %s", baseName);
        OPENVINO_THROW(error.what());
    }

#define vcl_symbol_statement(vcl_symbol)                                                                      \
    try {                                                                                                     \
        this->vcl_symbol = reinterpret_cast<decltype(&::vcl_symbol)>(ov::util::get_symbol(lib, #vcl_symbol)); \
    } catch (const std::runtime_error&) {                                                                     \
        _logger.debug("Failed to get %s from %s", #vcl_symbol, baseName);                                     \
        this->vcl_symbol = nullptr;                                                                           \
    }
    vcl_weak_symbols_list();
#undef vcl_symbol_statement
}

const std::shared_ptr<VCLApi> VCLApi::getInstance() {
    static std::map<std::filesystem::path, std::shared_ptr<VCLApi>> instances;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    const auto compilerLibraryPath = getCompilerLibraryPath();
    if (instances.find(compilerLibraryPath) == instances.end()) {
        instances[compilerLibraryPath] = std::make_shared<VCLApi>();
    }
    return instances[compilerLibraryPath];
}

}  // namespace intel_npu
