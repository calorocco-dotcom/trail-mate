#include "platform/ui/http_client_runtime.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace platform::ui::http_client
{
namespace
{

bool http_buffer_prefers_psram(const Request& request)
{
    return request.client == wifi_access::Client::RouteStorage ||
           request.client == wifi_access::Client::PackRepository;
}

class HeapCapsBuffer
{
  public:
    HeapCapsBuffer() = default;
    ~HeapCapsBuffer()
    {
        reset();
    }

    HeapCapsBuffer(const HeapCapsBuffer&) = delete;
    HeapCapsBuffer& operator=(const HeapCapsBuffer&) = delete;

    bool allocate(std::size_t bytes, bool prefer_psram)
    {
        reset();
        if (bytes == 0)
        {
            return false;
        }

        const uint32_t primary_caps = prefer_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                                   : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t secondary_caps = prefer_psram ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                                                     : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        data_ = static_cast<std::uint8_t*>(
            heap_caps_malloc_prefer(bytes, 2, primary_caps, secondary_caps));
        if (!data_)
        {
            return false;
        }
        size_ = bytes;
        return true;
    }

    void reset()
    {
        if (data_)
        {
            heap_caps_free(data_);
            data_ = nullptr;
        }
        size_ = 0;
    }

    std::uint8_t* data()
    {
        return data_;
    }

    std::size_t size() const
    {
        return size_;
    }

  private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

void log_memory_snapshot(const char* stage)
{
    std::printf("[HTTP][TLS] %s ram_free=%u ram_largest=%u psram_free=%u psram_largest=%u\n",
                stage ? stage : "state",
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

bool memory_preflight_ready(const Request& request)
{
    if (request.min_internal_free_bytes == 0 &&
        request.min_internal_largest_bytes == 0)
    {
        return true;
    }

    const std::uint32_t free_bytes = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t largest_bytes = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return free_bytes >= request.min_internal_free_bytes &&
           largest_bytes >= request.min_internal_largest_bytes;
}

bool require_memory_preflight(const Request& request,
                              const char* stage,
                              std::string& out_error)
{
    if (memory_preflight_ready(request))
    {
        return true;
    }
    log_memory_snapshot(stage);
    out_error = "Low HTTP memory";
    return false;
}

void configure_http_client(esp_http_client_config_t& config, const Request& request)
{
    config = esp_http_client_config_t{};
    config.url = request.url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : 30000;
    config.disable_auto_redirect = false;
    config.buffer_size = request.buffer_size > 0 ? request.buffer_size : 1024;
    config.buffer_size_tx = request.tx_buffer_size > 0 ? request.tx_buffer_size : 512;
    config.crt_bundle_attach = esp_crt_bundle_attach;
}

void set_stats(TransferStats* out_stats, int http_status, std::uint32_t bytes)
{
    if (!out_stats)
    {
        return;
    }
    out_stats->http_status = http_status;
    out_stats->bytes = bytes;
}

bool validate_request(const Request& request, std::string& out_error)
{
    if (!request.url || request.url[0] == '\0')
    {
        out_error = "Missing HTTP URL";
        return false;
    }
    if (request.client == wifi_access::Client::Unknown)
    {
        out_error = "Missing Wi-Fi access client";
        return false;
    }
    if (request.access_kind != wifi_access::AccessKind::HttpMetadata &&
        request.access_kind != wifi_access::AccessKind::HttpDownload &&
        request.access_kind != wifi_access::AccessKind::OtaDownload)
    {
        out_error = "Invalid HTTP access kind";
        return false;
    }
    return true;
}

} // namespace

bool get_text(const Request& request,
              std::string& out,
              std::string& out_error,
              TransferStats* out_stats)
{
    out.clear();
    return download(
        request,
        [](const std::uint8_t* data, std::size_t len, void* context)
        {
            auto* text = static_cast<std::string*>(context);
            text->append(reinterpret_cast<const char*>(data), len);
            return true;
        },
        &out,
        out_error,
        out_stats);
}

bool download(const Request& request,
              WriteCallback write,
              void* write_context,
              std::string& out_error,
              TransferStats* out_stats)
{
    out_error.clear();
    set_stats(out_stats, 0, 0);
    if (!validate_request(request, out_error))
    {
        return false;
    }
    if (!write)
    {
        out_error = "Missing HTTP writer";
        return false;
    }
    if (!require_memory_preflight(request, "preflight low memory", out_error))
    {
        return false;
    }

    wifi_access::Request access_request{};
    access_request.client = request.client;
    access_request.kind = request.access_kind;
    access_request.priority = request.priority;
    access_request.allow_connect = true;
    access_request.reason = request.reason;
    wifi_access::Lease lease = wifi_access::acquire(access_request);
    if (!lease.granted)
    {
        out_error = wifi_access::decision_name(lease.decision);
        return false;
    }

    wifi_access::ConnectResult connect_result{};
    if (!wifi_access::ensure_connected(access_request, &connect_result))
    {
        wifi_access::release(lease);
        out_error = wifi_access::decision_name(connect_result.decision);
        return false;
    }
    if (!require_memory_preflight(request, "pre-init low memory", out_error))
    {
        wifi_access::release(lease);
        return false;
    }

    esp_http_client_config_t config{};
    configure_http_client(config, request);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        wifi_access::release(lease);
        out_error = "Create HTTP client failed";
        return false;
    }

    const std::size_t buffer_size =
        request.buffer_size > 0 ? static_cast<std::size_t>(request.buffer_size) : 1024U;
    HeapCapsBuffer buffer;
    if (!buffer.allocate(buffer_size, http_buffer_prefers_psram(request)))
    {
        log_memory_snapshot("buffer alloc failed");
        esp_http_client_cleanup(client);
        wifi_access::release(lease);
        out_error = "Allocate HTTP buffer failed";
        return false;
    }

    bool ok = false;
    int http_status = 0;
    std::uint32_t bytes = 0;
    const esp_err_t open_err = esp_http_client_open(client, 0);
    if (open_err != ESP_OK)
    {
        log_memory_snapshot("open failed");
        out_error = "Open HTTP request failed";
    }
    else if (esp_http_client_fetch_headers(client) < 0)
    {
        log_memory_snapshot("headers failed");
        out_error = "Fetch HTTP headers failed";
    }
    else
    {
        http_status = esp_http_client_get_status_code(client);
        const int64_t content_length = esp_http_client_get_content_length(client);
        const std::uint32_t total_bytes =
            content_length > 0 && static_cast<std::uint64_t>(content_length) <= UINT32_MAX
                ? static_cast<std::uint32_t>(content_length)
                : 0U;
        if (http_status < 200 || http_status >= 300)
        {
            char text[48];
            std::snprintf(text, sizeof(text), "HTTP %d", http_status);
            out_error = text;
        }
        else if (request.max_bytes > 0 &&
                 content_length > 0 &&
                 static_cast<std::uint64_t>(content_length) > request.max_bytes)
        {
            out_error = "HTTP response too large";
        }
        else
        {
            if (request.progress)
            {
                request.progress(0, total_bytes, request.progress_context);
            }
            while (out_error.empty())
            {
                if (wifi_access::lease_revoked(lease))
                {
                    out_error = wifi_access::decision_name(
                        wifi_access::Decision::CallExclusive);
                    break;
                }
                const int read = esp_http_client_read(
                    client,
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<int>(buffer.size()));
                if (read < 0)
                {
                    log_memory_snapshot("read failed");
                    out_error = "Read HTTP response failed";
                    break;
                }
                if (read == 0)
                {
                    ok = true;
                    break;
                }
                if (request.max_bytes > 0 &&
                    bytes + static_cast<std::uint32_t>(read) > request.max_bytes)
                {
                    out_error = "HTTP response too large";
                    break;
                }
                if (!write(buffer.data(), static_cast<std::size_t>(read), write_context))
                {
                    out_error = "Write HTTP response failed";
                    break;
                }
                bytes += static_cast<std::uint32_t>(read);
                if (request.progress)
                {
                    request.progress(bytes, total_bytes, request.progress_context);
                }
                if (wifi_access::lease_revoked(lease))
                {
                    out_error = wifi_access::decision_name(
                        wifi_access::Decision::CallExclusive);
                    break;
                }
            }
        }
    }

    (void)esp_http_client_close(client);
    (void)esp_http_client_cleanup(client);
    wifi_access::release(lease);
    set_stats(out_stats, http_status, bytes);
    return ok;
}

} // namespace platform::ui::http_client
