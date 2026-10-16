// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Standalone Vulkan capability probe for a selected ICD manifest and loader.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kRequiredApiVersion = VK_API_VERSION_1_3;

struct Capabilities {
    uint32_t api_version{};
    std::string device_name;
    bool swapchain{};
    bool push_descriptor{};
    bool vertex_attribute_divisor{};
    bool robustness2{};
    bool robust_buffer_access2{};
    bool robust_image_access2{};
    bool null_descriptor{};
};

bool Ready(const Capabilities& caps) {
    return caps.api_version >= kRequiredApiVersion && caps.swapchain && caps.push_descriptor &&
           caps.vertex_attribute_divisor && caps.robustness2 && caps.robust_buffer_access2 &&
           caps.robust_image_access2 && caps.null_descriptor;
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [&](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::string JsonEscape(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2);
    for (const unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                result += "\\u00";
                result += hex[c >> 4];
                result += hex[c & 0xf];
            } else {
                result += static_cast<char>(c);
            }
        }
    }
    return result;
}

void PrintDeviceJson(const Capabilities& caps, bool comma) {
    if (comma) {
        std::cout << ",\n";
    }
    std::cout << "    {\"name\":\"" << JsonEscape(caps.device_name) << "\",\"apiVersion\":\""
              << VK_VERSION_MAJOR(caps.api_version) << '.' << VK_VERSION_MINOR(caps.api_version)
              << '.' << VK_VERSION_PATCH(caps.api_version) << "\",\"apiVersionValue\":"
              << caps.api_version << ",\"requiredExtensions\":{\"swapchain\":"
              << (caps.swapchain ? "true" : "false") << ",\"pushDescriptor\":"
              << (caps.push_descriptor ? "true" : "false")
              << ",\"vertexAttributeDivisor\":"
              << (caps.vertex_attribute_divisor ? "true" : "false")
              << ",\"robustness2\":" << (caps.robustness2 ? "true" : "false")
              << "},\"robustness2Features\":{\"robustBufferAccess2\":"
              << (caps.robust_buffer_access2 ? "true" : "false")
              << ",\"robustImageAccess2\":"
              << (caps.robust_image_access2 ? "true" : "false") << ",\"nullDescriptor\":"
              << (caps.null_descriptor ? "true" : "false") << "},\"readyForVulkan13\":"
              << (Ready(caps) ? "true" : "false") << "}";
}

int SelfTest() {
    Capabilities baseline{.api_version = VK_API_VERSION_1_3,
                          .device_name = "probe \"quote\" \\\n",
                          .swapchain = true,
                          .push_descriptor = true,
                          .vertex_attribute_divisor = true,
                          .robustness2 = true,
                          .robust_buffer_access2 = true,
                          .robust_image_access2 = true,
                          .null_descriptor = true};
    const auto negative = [&](auto mutate) {
        auto missing = baseline;
        mutate(missing);
        return !Ready(missing);
    };
    const bool api_negative = negative([](auto& c) { c.api_version = VK_API_VERSION_1_2; });
    const bool swapchain_negative = negative([](auto& c) { c.swapchain = false; });
    const bool push_negative = negative([](auto& c) { c.push_descriptor = false; });
    const bool divisor_negative = negative([](auto& c) { c.vertex_attribute_divisor = false; });
    const bool robustness_extension_negative = negative([](auto& c) { c.robustness2 = false; });
    const bool buffer_negative = negative([](auto& c) { c.robust_buffer_access2 = false; });
    const bool image_negative = negative([](auto& c) { c.robust_image_access2 = false; });
    const bool null_negative = negative([](auto& c) { c.null_descriptor = false; });
    const bool passed = Ready(baseline) && api_negative && swapchain_negative && push_negative &&
                        divisor_negative && robustness_extension_negative && buffer_negative &&
                        image_negative && null_negative;
    std::cout << "{\"selfTest\":true,\"passed\":" << (passed ? "true" : "false")
              << ",\"positive\":" << (Ready(baseline) ? "true" : "false")
              << ",\"missing\":{\"apiVersion\":" << (api_negative ? "true" : "false")
              << ",\"swapchain\":" << (swapchain_negative ? "true" : "false")
              << ",\"pushDescriptor\":" << (push_negative ? "true" : "false")
              << ",\"vertexAttributeDivisor\":" << (divisor_negative ? "true" : "false")
              << ",\"robustness2\":" << (robustness_extension_negative ? "true" : "false")
              << ",\"robustBufferAccess2\":" << (buffer_negative ? "true" : "false")
              << ",\"robustImageAccess2\":" << (image_negative ? "true" : "false")
              << ",\"nullDescriptor\":" << (null_negative ? "true" : "false")
              << "},\"jsonEscape\":\"" << JsonEscape(baseline.device_name) << "\"}\n";
    return passed ? 0 : 1;
}

template <typename T>
T LoadFunction(PFN_vkGetInstanceProcAddr get_proc, VkInstance instance, const char* name) {
    return reinterpret_cast<T>(get_proc(instance, name));
}

template <typename T>
bool Enumerate(T enumerate, std::vector<VkExtensionProperties>& values) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t count = 0;
        VkResult result = enumerate(&count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            return false;
        }
        values.resize(count);
        result = enumerate(&count, values.data());
        if (result == VK_SUCCESS) {
            values.resize(count);
            return true;
        }
        if (result != VK_INCOMPLETE) {
            return false;
        }
    }
    return false;
}

int RunProbe(const char* manifest_path, const char* loader_path) {
    const std::filesystem::path manifest{manifest_path};
    const std::filesystem::path loader{loader_path};
    if (!manifest.is_absolute() || !loader.is_absolute()) {
        std::cerr << "ICD manifest and Vulkan loader paths must be absolute\n";
        return 2;
    }
    if (setenv("VK_DRIVER_FILES", manifest.c_str(), 1) != 0) {
        std::perror("setenv VK_DRIVER_FILES");
        return 2;
    }
    void* library = dlopen(loader.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << "dlopen failed: " << dlerror() << '\n';
        return 2;
    }
    const auto close_library = [&] { dlclose(library); };
    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
    if (!get_proc) {
        std::cerr << "Vulkan loader does not export vkGetInstanceProcAddr\n";
        close_library();
        return 2;
    }

    auto enumerate_instance_extensions = LoadFunction<PFN_vkEnumerateInstanceExtensionProperties>(
        get_proc, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    auto create_instance = LoadFunction<PFN_vkCreateInstance>(get_proc, VK_NULL_HANDLE, "vkCreateInstance");
    auto enumerate_instance_version = LoadFunction<PFN_vkEnumerateInstanceVersion>(
        get_proc, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (!enumerate_instance_extensions || !create_instance) {
        std::cerr << "Vulkan loader is missing required global entry points\n";
        close_library();
        return 2;
    }

    std::vector<VkExtensionProperties> instance_extensions;
    if (!Enumerate([&](uint32_t* count, VkExtensionProperties* values) {
            return enumerate_instance_extensions(nullptr, count, values);
        }, instance_extensions)) {
        std::cerr << "vkEnumerateInstanceExtensionProperties failed\n";
        close_library();
        return 2;
    }
    uint32_t loader_api_version = VK_API_VERSION_1_0;
    if (enumerate_instance_version && enumerate_instance_version(&loader_api_version) != VK_SUCCESS) {
        loader_api_version = VK_API_VERSION_1_0;
    }
    const uint32_t requested_api_version = std::min(loader_api_version, kRequiredApiVersion);
    const bool portability = HasExtension(instance_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    const char* portability_extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    const VkApplicationInfo app_info{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .pApplicationName = "shadPS4 Vulkan driver probe",
                                     .applicationVersion = 1,
                                     .pEngineName = "standalone capability probe",
                                     .engineVersion = 1,
                                     .apiVersion = requested_api_version};
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
                             : static_cast<VkInstanceCreateFlags>(0),
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = portability ? 1u : 0u,
        .ppEnabledExtensionNames = portability ? &portability_extension : nullptr};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed with VkResult " << result << '\n';
        close_library();
        return 2;
    }
    const auto destroy_instance = LoadFunction<PFN_vkDestroyInstance>(get_proc, instance, "vkDestroyInstance");
    const auto enumerate_physical_devices = LoadFunction<PFN_vkEnumeratePhysicalDevices>(get_proc, instance, "vkEnumeratePhysicalDevices");
    const auto get_properties = LoadFunction<PFN_vkGetPhysicalDeviceProperties>(get_proc, instance, "vkGetPhysicalDeviceProperties");
    const auto enumerate_device_extensions = LoadFunction<PFN_vkEnumerateDeviceExtensionProperties>(get_proc, instance, "vkEnumerateDeviceExtensionProperties");
    auto get_features2 = LoadFunction<PFN_vkGetPhysicalDeviceFeatures2>(get_proc, instance, "vkGetPhysicalDeviceFeatures2");
    if (!get_features2) {
        get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            get_proc(instance, "vkGetPhysicalDeviceFeatures2KHR"));
    }
    if (!destroy_instance || !enumerate_physical_devices || !get_properties ||
        !enumerate_device_extensions || !get_features2) {
        std::cerr << "Vulkan loader is missing required instance entry points\n";
        if (destroy_instance) destroy_instance(instance, nullptr);
        close_library();
        return 2;
    }

    uint32_t device_count = 0;
    result = enumerate_physical_devices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        std::cerr << "No Vulkan physical devices found (VkResult " << result << ")\n";
        destroy_instance(instance, nullptr);
        close_library();
        return 2;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    result = enumerate_physical_devices(instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        std::cerr << "Physical device enumeration failed with VkResult " << result << '\n';
        destroy_instance(instance, nullptr);
        close_library();
        return 2;
    }
    devices.resize(device_count);

    std::vector<Capabilities> all_caps;
    all_caps.reserve(devices.size());
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        get_properties(device, &properties);
        std::vector<VkExtensionProperties> extensions;
        if (!Enumerate([&](uint32_t* count, VkExtensionProperties* values) {
                return enumerate_device_extensions(device, nullptr, count, values);
            }, extensions)) {
            std::cerr << "Device extension enumeration failed\n";
            destroy_instance(instance, nullptr);
            close_library();
            return 2;
        }
        const bool robustness2 = HasExtension(extensions, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        VkPhysicalDeviceRobustness2FeaturesEXT robustness{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .pNext = nullptr};
        if (robustness2) {
            VkPhysicalDeviceFeatures2 features{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &robustness};
            get_features2(device, &features);
        }
        all_caps.push_back(Capabilities{
            .api_version = properties.apiVersion,
            .device_name = properties.deviceName,
            .swapchain = HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME),
            .push_descriptor = HasExtension(extensions, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME),
            .vertex_attribute_divisor = HasExtension(extensions, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME),
            .robustness2 = robustness2,
            .robust_buffer_access2 = robustness.robustBufferAccess2 == VK_TRUE,
            .robust_image_access2 = robustness.robustImageAccess2 == VK_TRUE,
            .null_descriptor = robustness.nullDescriptor == VK_TRUE});
    }

    bool any_ready_device = false;
    for (const auto& caps : all_caps) any_ready_device |= Ready(caps);
    const bool ready = loader_api_version >= kRequiredApiVersion && any_ready_device;
    std::cout << "{\"loaderApiVersion\":\"" << VK_VERSION_MAJOR(loader_api_version) << '.'
              << VK_VERSION_MINOR(loader_api_version) << '.' << VK_VERSION_PATCH(loader_api_version)
              << "\",\"portabilityEnumeration\":" << (portability ? "true" : "false")
              << ",\"deviceCount\":" << all_caps.size() << ",\"readyForVulkan13\":"
              << (ready ? "true" : "false") << ",\"devices\":[\n";
    for (size_t i = 0; i < all_caps.size(); ++i) PrintDeviceJson(all_caps[i], i != 0);
    std::cout << "\n]}\n";

    destroy_instance(instance, nullptr);
    close_library();
    return ready ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
        return SelfTest();
    }
    if (argc != 3) {
        std::cerr << "Usage: probe_vulkan_driver <absolute-icd-manifest> <absolute-vulkan-loader>\n"
                     "       probe_vulkan_driver --self-test\n";
        return 2;
    }
    return RunProbe(argv[1], argv[2]);
}
