#ifdef _WIN32
#  define WINVER 0x0500
#  define _WIN32_WINNT 0x0500
#  include <windows.h>
#endif
#ifdef __linux__
#  include <fcntl.h>
#  include <filesystem>
#  include <string>
#  include <sys/mman.h>
#  include <unistd.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "scssdk_input.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_input_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_input_ats.h"

#include "inputs.h"

using namespace std;

#ifdef _WIN32
HANDLE file_map_h = nullptr;
uint8_t* shm_buff_ptr = nullptr;

void initialize_shared_memory(const unsigned int shm_size)
{
    const char* mem_name = "Local\\SCSControls";

    file_map_h = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        shm_size,
        mem_name);
    if (file_map_h == NULL) {
        file_map_h = nullptr;
        throw runtime_error("Failed to create file mapping");
    }

    shm_buff_ptr = (uint8_t*)MapViewOfFile(file_map_h, FILE_MAP_ALL_ACCESS, 0, 0, shm_size);
    if (shm_buff_ptr == NULL) {
        throw runtime_error("Failed to mmap");
        shm_buff_ptr = nullptr;
        CloseHandle(file_map_h);
        file_map_h = nullptr;
    }

    memset(shm_buff_ptr, 0, shm_size);
}

void deinitialize_shared_memory()
{
    if (shm_buff_ptr) {
        UnmapViewOfFile(shm_buff_ptr);
        shm_buff_ptr = nullptr;
    }

    if (file_map_h) {
        CloseHandle(file_map_h);
        file_map_h = nullptr;
    }
}
#endif

#ifdef __linux__
int shm_fd = -1;
unsigned int m_shm_size = 0;
uint8_t* shm_buff_ptr = nullptr;

void initialize_shared_memory(const unsigned int shm_size)
{
    const filesystem::path scs_shm_path = "/dev/shm/SCS/";
    const string shm_file = scs_shm_path/"SCSControls";

    if (!exists(scs_shm_path)) {
        create_directory(scs_shm_path);
    }

    shm_fd = open(shm_file.c_str(), O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        throw runtime_error("Failed to create shm file");
    }

    // Truncate and zero the shm file
    if (ftruncate(shm_fd, shm_size) == -1) {
        close(shm_fd);
        throw runtime_error("Failed to truncate shm file");
    }

    shm_buff_ptr = (uint8_t*)mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_buff_ptr == MAP_FAILED) {
        close(shm_fd);
        throw runtime_error("Failed mmap shm");
    }
    m_shm_size = shm_size;
}

void deinitialize_shared_memory()
{
    if (shm_buff_ptr) {
        munmap(shm_buff_ptr, m_shm_size);
        m_shm_size = 0;
        shm_buff_ptr = nullptr;
    }

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}
#endif

struct input_context_t
{
    unsigned int input_idx = 0;
    unsigned int shm_offset = 0;
};

// input_event_callback will be called multiple times for each frame until it returns SCS_RESULT_not_found
SCSAPI_RESULT input_event_callback(scs_input_event_t *const event_info, const scs_u32_t flags, const scs_context_t context)
{
    unsigned int input_count = sizeof(inputs) / sizeof(scs_input_device_input_t);
    input_context_t &input_context = *static_cast<input_context_t *>(context);

    // Start processing inputs for current frame
    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        input_context.input_idx = 0;
        input_context.shm_offset = 0;
    }

    // If we have processed all inputs, return not found
    if (input_context.input_idx >= input_count) {
        return SCS_RESULT_not_found;
    }

    event_info->input_index = input_context.input_idx;
    scs_input_device_input_t input = inputs[input_context.input_idx];

    if (input.value_type == SCS_VALUE_TYPE_float) {
        float read_val;
        memcpy(&read_val, shm_buff_ptr + input_context.shm_offset, sizeof(float));
        event_info->value_float.value = clamp(float(-1), read_val, float(1));
        input_context.shm_offset += sizeof(float);
    } else if (input.value_type == SCS_VALUE_TYPE_bool) {
        bool read_val;
        memcpy(&read_val, shm_buff_ptr + input_context.shm_offset, sizeof(bool));
        event_info->value_bool.value = read_val;
        input_context.shm_offset += sizeof(bool);
    }

    input_context.input_idx++;

    return SCS_RESULT_ok;
}

SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t *const params)
{
    // We currently support only one version.
    if (version != SCS_INPUT_VERSION_1_00) {
        return SCS_RESULT_unsupported;
    }

    const scs_input_init_params_v100_t *const version_params = static_cast<const scs_input_init_params_v100_t *>(params);

    // Setup the device information. The name of the input matches the name of the
    // mix as seen in controls.sii. Note that only some inputs are supported this way.
    // See documentation of SCS_INPUT_DEVICE_TYPE_semantical
    input_context_t input_context;
    scs_input_device_t device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.name = "laneassist";
    device_info.display_name = "ETS2 Lane Assist";
    device_info.type = SCS_INPUT_DEVICE_TYPE_semantical;
    device_info.input_count = sizeof(inputs) / sizeof(scs_input_device_input_t);
    device_info.inputs = inputs;
    device_info.input_event_callback = input_event_callback;
    device_info.callback_context = &input_context;

    unsigned int shm_size = 0;
    for (const scs_input_device_input_t input : inputs) {
        if (input.value_type == SCS_VALUE_TYPE_float) {
            shm_size += sizeof(float);
        } else if (input.value_type == SCS_VALUE_TYPE_bool) {
            shm_size += sizeof(bool);
        }
    }

    try {
        initialize_shared_memory(shm_size);
    } catch (const exception& e) {
        string init_error = string("Failed to init shm: ") + e.what();
        version_params->common.log(SCS_LOG_TYPE_error, init_error.c_str());
        return SCS_RESULT_generic_error;
    }

    if (version_params->register_device(&device_info) != SCS_RESULT_ok) {
        version_params->common.log(SCS_LOG_TYPE_error, "Unable to register device");
        return SCS_RESULT_generic_error;
    }

    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_input_shutdown()
{
    deinitialize_shared_memory();
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason_for_call, LPVOID reseved)
{
    if (reason_for_call == DLL_PROCESS_DETACH) {
        deinitialize_shared_memory();
    }

    return true;
}
#endif

#ifdef __linux__
void __attribute__ ((destructor)) unload()
{
    deinitialize_shared_memory();
}
#endif
