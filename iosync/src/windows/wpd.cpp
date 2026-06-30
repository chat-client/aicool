#include "iosync.h"

#include <PortableDevice.h>
#include <PortableDeviceApi.h>
#include <atlbase.h>
#include <comdef.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <propkey.h>
#include <cwctype>
#include <string>
#include <vector>

namespace {

struct ComInit {
    HRESULT hr;
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit()
    {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(chars - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), chars);
    return out;
}

std::string hr_text(HRESULT hr)
{
    _com_error error(hr);
    return narrow(error.ErrorMessage());
}

bool looks_like_ios(const std::wstring& name)
{
    return name.find(L"iPhone") != std::wstring::npos ||
           name.find(L"iPad") != std::wstring::npos ||
           name.find(L"Apple") != std::wstring::npos;
}

std::vector<std::wstring> get_device_ids(std::string& error)
{
    CComPtr<IPortableDeviceManager> manager;
    HRESULT hr = manager.CoCreateInstance(CLSID_PortableDeviceManager);
    if (FAILED(hr)) {
        error = "failed to create PortableDeviceManager: " + hr_text(hr);
        return {};
    }

    DWORD count = 0;
    hr = manager->GetDevices(nullptr, &count);
    if (FAILED(hr)) {
        error = "failed to enumerate portable devices: " + hr_text(hr);
        return {};
    }
    if (count == 0) {
        return {};
    }

    std::vector<PWSTR> raw(count, nullptr);
    hr = manager->GetDevices(raw.data(), &count);
    if (FAILED(hr)) {
        error = "failed to read portable device ids: " + hr_text(hr);
        return {};
    }

    std::vector<std::wstring> ids;
    for (DWORD i = 0; i < count; ++i) {
        if (raw[i]) {
            ids.emplace_back(raw[i]);
            CoTaskMemFree(raw[i]);
        }
    }
    return ids;
}

std::wstring friendly_name(IPortableDeviceManager* manager, const std::wstring& id)
{
    DWORD chars = 0;
    manager->GetDeviceFriendlyName(id.c_str(), nullptr, &chars);
    if (chars == 0) {
        return id;
    }
    std::wstring name(chars, L'\0');
    if (FAILED(manager->GetDeviceFriendlyName(id.c_str(), name.data(), &chars))) {
        return id;
    }
    while (!name.empty() && name.back() == L'\0') {
        name.pop_back();
    }
    return name;
}

CComPtr<IPortableDevice> open_device(int index, std::string& error)
{
    std::vector<std::wstring> ids = get_device_ids(error);
    if (!error.empty()) {
        return {};
    }

    CComPtr<IPortableDeviceManager> manager;
    HRESULT hr = manager.CoCreateInstance(CLSID_PortableDeviceManager);
    if (FAILED(hr)) {
        error = "failed to create PortableDeviceManager: " + hr_text(hr);
        return {};
    }

    std::vector<std::wstring> candidates;
    for (const auto& id : ids) {
        const auto name = friendly_name(manager, id);
        if (looks_like_ios(name)) {
            candidates.push_back(id);
        }
    }
    if (candidates.empty()) {
        candidates = ids;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= candidates.size()) {
        error = "device index is out of range";
        return {};
    }

    CComPtr<IPortableDeviceValues> clientInfo;
    hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&clientInfo));
    if (FAILED(hr)) {
        error = "failed to create WPD client info: " + hr_text(hr);
        return {};
    }
    clientInfo->SetStringValue(WPD_CLIENT_NAME, L"iosync");
    clientInfo->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
    clientInfo->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
    clientInfo->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);

    CComPtr<IPortableDevice> device;
    hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_PortableDevice, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&device));
    }
    if (FAILED(hr)) {
        error = "failed to create PortableDevice: " + hr_text(hr);
        return {};
    }
    hr = device->Open(candidates[static_cast<std::size_t>(index)].c_str(), clientInfo);
    if (FAILED(hr)) {
        error = "failed to open portable device; unlock the phone and trust this PC: " + hr_text(hr);
        return {};
    }
    return device;
}

std::wstring get_string_property(IPortableDeviceProperties* properties, const std::wstring& objectId, const PROPERTYKEY& key)
{
    CComPtr<IPortableDeviceKeyCollection> keys;
    if (FAILED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&keys)))) {
        return {};
    }
    keys->Add(key);

    CComPtr<IPortableDeviceValues> values;
    if (FAILED(properties->GetValues(objectId.c_str(), keys, &values))) {
        return {};
    }

    PWSTR raw = nullptr;
    if (FAILED(values->GetStringValue(key, &raw)) || raw == nullptr) {
        return {};
    }
    std::wstring out(raw);
    CoTaskMemFree(raw);
    return out;
}

ULONGLONG get_u64_property(IPortableDeviceProperties* properties, const std::wstring& objectId, const PROPERTYKEY& key)
{
    CComPtr<IPortableDeviceKeyCollection> keys;
    if (FAILED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&keys)))) {
        return 0;
    }
    keys->Add(key);

    CComPtr<IPortableDeviceValues> values;
    if (FAILED(properties->GetValues(objectId.c_str(), keys, &values))) {
        return 0;
    }

    ULONGLONG out = 0;
    values->GetUnsignedLargeIntegerValue(key, &out);
    return out;
}

GUID get_guid_property(IPortableDeviceProperties* properties, const std::wstring& objectId, const PROPERTYKEY& key)
{
    CComPtr<IPortableDeviceKeyCollection> keys;
    if (FAILED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&keys)))) {
        return GUID_NULL;
    }
    keys->Add(key);

    CComPtr<IPortableDeviceValues> values;
    if (FAILED(properties->GetValues(objectId.c_str(), keys, &values))) {
        return GUID_NULL;
    }

    GUID out = GUID_NULL;
    values->GetGuidValue(key, &out);
    return out;
}

bool is_media_format(REFGUID format)
{
    return IsEqualGUID(format, WPD_OBJECT_FORMAT_EXIF) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_JFIF) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_PNG) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_BMP) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_GIF) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_TIFF) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_WMA) ||
           IsEqualGUID(format, WPD_OBJECT_FORMAT_WMV);
}

std::wstring lower_copy(std::wstring text)
{
    for (auto& ch : text) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return text;
}

bool has_extension(const std::wstring& name, const std::vector<std::wstring>& extensions)
{
    const auto lower = lower_copy(name);
    for (const auto& ext : extensions) {
        if (lower.size() >= ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
            return true;
        }
    }
    return false;
}

bool is_media_name(const std::wstring& name)
{
    static const std::vector<std::wstring> extensions = {
        L".jpg", L".jpeg", L".heic", L".heif", L".png", L".gif", L".tif", L".tiff",
        L".mov", L".mp4", L".m4v", L".avi", L".3gp"
    };
    return has_extension(name, extensions);
}

std::string kind_from_name_or_format(const std::wstring& name, REFGUID format)
{
    if (IsEqualGUID(format, WPD_OBJECT_FORMAT_WMV) ||
        has_extension(name, {L".mov", L".mp4", L".m4v", L".avi", L".3gp"})) {
        return "video";
    }
    return "photo";
}

void enumerate_children(IPortableDeviceContent* content,
                        IPortableDeviceProperties* properties,
                        const std::wstring& parentId,
                        std::vector<iosync::MediaItem>& out)
{
    CComPtr<IEnumPortableDeviceObjectIDs> enumIds;
    if (FAILED(content->EnumObjects(0, parentId.c_str(), nullptr, &enumIds))) {
        return;
    }

    while (true) {
        PWSTR objectId = nullptr;
        DWORD fetched = 0;
        const HRESULT hr = enumIds->Next(1, &objectId, &fetched);
        if (FAILED(hr) || fetched == 0) {
            break;
        }

        std::wstring id(objectId);
        CoTaskMemFree(objectId);

        const GUID contentType = get_guid_property(properties, id, WPD_OBJECT_CONTENT_TYPE);
        const GUID format = get_guid_property(properties, id, WPD_OBJECT_FORMAT);
        if (IsEqualGUID(contentType, WPD_CONTENT_TYPE_FOLDER) ||
            IsEqualGUID(contentType, WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT)) {
            enumerate_children(content, properties, id, out);
            continue;
        }

        std::wstring name = get_string_property(properties, id, WPD_OBJECT_ORIGINAL_FILE_NAME);
        if (name.empty()) {
            name = get_string_property(properties, id, WPD_OBJECT_NAME);
        }

        if (is_media_format(format) || is_media_name(name) ||
            IsEqualGUID(contentType, WPD_CONTENT_TYPE_IMAGE) ||
            IsEqualGUID(contentType, WPD_CONTENT_TYPE_VIDEO)) {
            iosync::MediaItem item;
            item.id = narrow(id);
            item.name = narrow(name);
            item.kind = IsEqualGUID(contentType, WPD_CONTENT_TYPE_VIDEO) ? "video" : kind_from_name_or_format(name, format);
            item.size = get_u64_property(properties, id, WPD_OBJECT_SIZE);
            out.push_back(item);
        }
    }
}

std::vector<iosync::MediaItem> collect_media(IPortableDevice* device, std::string& error)
{
    CComPtr<IPortableDeviceContent> content;
    HRESULT hr = device->Content(&content);
    if (FAILED(hr)) {
        error = "failed to access device content: " + hr_text(hr);
        return {};
    }

    CComPtr<IPortableDeviceProperties> properties;
    hr = content->Properties(&properties);
    if (FAILED(hr)) {
        error = "failed to access device properties: " + hr_text(hr);
        return {};
    }

    std::vector<iosync::MediaItem> items;
    enumerate_children(content, properties, WPD_DEVICE_OBJECT_ID, items);
    return items;
}

bool copy_object(IPortableDevice* device, const iosync::MediaItem& item, const std::filesystem::path& outDir, std::string& error)
{
    CComPtr<IPortableDeviceContent> content;
    HRESULT hr = device->Content(&content);
    if (FAILED(hr)) {
        error = "failed to access device content: " + hr_text(hr);
        return false;
    }

    CComPtr<IPortableDeviceResources> resources;
    hr = content->Transfer(&resources);
    if (FAILED(hr)) {
        error = "failed to access device transfer resources: " + hr_text(hr);
        return false;
    }

    CComPtr<IStream> stream;
    DWORD optimalSize = 0;
    const std::wstring objectId = widen(item.id);
    hr = resources->GetStream(objectId.c_str(), WPD_RESOURCE_DEFAULT, STGM_READ, &optimalSize, &stream);
    if (FAILED(hr)) {
        error = "failed to open media stream for " + item.name + ": " + hr_text(hr);
        return false;
    }

    std::filesystem::create_directories(outDir);
    const auto outputPath = outDir / widen(iosync::sanitize_filename(item.name));
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        error = "failed to create output file: " + narrow(outputPath.wstring());
        return false;
    }

    const DWORD bufferSize = optimalSize > 0 ? optimalSize : 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    while (true) {
        ULONG read = 0;
        hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &read);
        if (FAILED(hr)) {
            error = "failed while reading " + item.name + ": " + hr_text(hr);
            return false;
        }
        if (read == 0) {
            break;
        }
        output.write(buffer.data(), read);
        if (!output) {
            error = "failed while writing output file: " + narrow(outputPath.wstring());
            return false;
        }
    }
    return true;
}

} // namespace

namespace iosync {

std::vector<Device> list_devices(const Options& options, std::string& error)
{
    (void)options;
    ComInit com;
    if (FAILED(com.hr)) {
        error = "failed to initialize COM: " + hr_text(com.hr);
        return {};
    }

    CComPtr<IPortableDeviceManager> manager;
    HRESULT hr = manager.CoCreateInstance(CLSID_PortableDeviceManager);
    if (FAILED(hr)) {
        error = "failed to create PortableDeviceManager: " + hr_text(hr);
        return {};
    }

    const auto ids = get_device_ids(error);
    std::vector<Device> devices;
    for (const auto& id : ids) {
        const auto name = friendly_name(manager, id);
        if (!looks_like_ios(name)) {
            continue;
        }
        devices.push_back(Device{narrow(id), narrow(name)});
    }
    return devices;
}

std::vector<MediaItem> list_media(const Options& options, std::string& error)
{
    ComInit com;
    if (FAILED(com.hr)) {
        error = "failed to initialize COM: " + hr_text(com.hr);
        return {};
    }
    CComPtr<IPortableDevice> device = open_device(options.device_index, error);
    if (!device) {
        return {};
    }
    return collect_media(device, error);
}

bool list_media_stream(const Options& options, const MediaItemCallback& callback, std::string& error)
{
    const auto items = list_media(options, error);
    if (!error.empty()) {
        return false;
    }
    for (const auto& item : items) {
        callback(item);
    }
    return true;
}

bool export_media(const Options& options, std::string& error)
{
    ComInit com;
    if (FAILED(com.hr)) {
        error = "failed to initialize COM: " + hr_text(com.hr);
        return false;
    }
    CComPtr<IPortableDevice> device = open_device(options.device_index, error);
    if (!device) {
        return false;
    }

    auto items = collect_media(device, error);
    if (!error.empty()) {
        return false;
    }

    bool exported = false;
    std::size_t exportedCount = 0;
    for (const auto& item : items) {
        if (!options.all && item.id != options.object_id) {
            continue;
        }
        if (!copy_object(device, item, std::filesystem::path(widen(options.output_dir)), error)) {
            return false;
        }
        exportedCount += 1;
        print_export_progress(exportedCount, item);
        exported = true;
    }

    if (!exported) {
        error = options.all ? "no media files found" : "object id was not found";
    }
    if (exported) {
        std::cerr << "iosync: export completed; exported "
                  << exportedCount << " file(s) to " << options.output_dir << '\n';
    }
    return exported;
}

} // namespace iosync
