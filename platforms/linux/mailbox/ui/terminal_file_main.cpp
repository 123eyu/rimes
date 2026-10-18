// Disposable image decoder: no GTK window, network fetch, or document execution.
#include "capsule_resources.hpp"
#include "capsule_rich_text.hpp"
#include "capsule_text.hpp"
#include "terminal_files.hpp"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sys/resource.h>
using namespace rimes::capsule;
using rimes::mailbox::TerminalContract;
namespace {
void Limit(int resource, rlim_t value) {
    const rlimit limit{value, value};
    if (setrlimit(resource, &limit))
        throw std::runtime_error("无法限制文件预览资源");
}
struct Size {
    int width = 0, height = 0;
    bool invalid = false;
};
} // namespace
int main(int argc, char** argv) {
    try {
        Limit(RLIMIT_AS, 768ULL * 1048576);
        Limit(RLIMIT_CPU, 5);
        Limit(RLIMIT_CORE, 0);
        Limit(RLIMIT_FSIZE, 0);
        if (argc != 2)
            return 2;
        const std::filesystem::path path(argv[1]);
        std::atomic<bool> cancelled{false};
        const auto& limits = TerminalContract().at("limits");
        auto bytes = ReadResource(path, limits.at("maximumFileBytes"), cancelled);
        const auto begins = [&](std::string_view magic) {
            return bytes.compare(0, magic.size(), magic) == 0;
        };
        const bool raster =
            begins(std::string_view("\x89PNG\r\n\x1a\n", 8)) ||
            begins(std::string_view("\xff\xd8\xff", 3)) || begins("GIF87a") || begins("GIF89a") ||
            begins("BM") || begins(std::string_view("II\x2a\0", 4)) ||
            begins(std::string_view("MM\0\x2a", 4)) || begins(std::string_view("\0\0\1\0", 4)) ||
            (begins("RIFF") && bytes.size() >= 12 && bytes.substr(8, 4) == "WEBP");
        if (!raster) {
            if (bytes.size() > limits.at("maximumTextBytes").get<std::size_t>() ||
                !g_utf8_validate(bytes.data(), bytes.size(), nullptr) ||
                bytes.find('\0') != bytes.npos || TrimText(bytes).empty())
                throw std::runtime_error("暂时支持图片和不超过 1 MB 的 UTF-8 文本。");
            std::cout << nlohmann::json{{"body", bytes}, {"png", ""}}.dump();
            return 0;
        }
        auto* loader = gdk_pixbuf_loader_new();
        Size size;
        g_signal_connect(
            loader, "size-prepared",
            G_CALLBACK(+[](GdkPixbufLoader* loader, int w, int h, gpointer p) {
                auto& size = *static_cast<Size*>(p);
                size.width = w;
                size.height = h;
                size.invalid =
                    w <= 0 || h <= 0 ||
                    w > TerminalContract().at("limits").at("maximumImageDimension").get<int>() ||
                    h > TerminalContract().at("limits").at("maximumImageDimension").get<int>() ||
                    int64_t(w) * h >
                        TerminalContract().at("limits").at("maximumImagePixels").get<int>();
                if (size.invalid)
                    gdk_pixbuf_loader_set_size(loader, 1, 1);
            }),
            &size);
        GError* error = nullptr;
        const bool written = gdk_pixbuf_loader_write(
            loader, reinterpret_cast<const guchar*>(bytes.data()), bytes.size(), &error);
        g_clear_error(&error);
        const bool closed = gdk_pixbuf_loader_close(loader, &error);
        g_clear_error(&error);
        auto* format = gdk_pixbuf_loader_get_format(loader);
        auto* name = format ? gdk_pixbuf_format_get_name(format) : nullptr;
        const std::set<std::string> supported{"png", "jpeg", "gif", "webp", "bmp", "tiff", "ico"};
        const bool image = name && supported.count(name);
        g_free(name);
        std::string png, body;
        auto* pixbuf = written && closed && image && !size.invalid
                           ? gdk_pixbuf_loader_get_pixbuf(loader)
                           : nullptr;
        if (pixbuf) {
            gchar* output = nullptr;
            gsize count = 0;
            if (gdk_pixbuf_save_to_buffer(pixbuf, &output, &count, "png", &error, nullptr) &&
                count <= limits.at("maximumFileBytes").get<std::size_t>())
                png.assign(output, count);
            g_free(output);
            g_clear_error(&error);
            body = path.filename().string() + "\n" + std::to_string(size.width) + " × " +
                   std::to_string(size.height);
        }
        g_object_unref(loader);
        if (png.empty())
            throw std::runtime_error("这张图片过大或无法读取。");
        std::cout << nlohmann::json{{"body", body}, {"png", Base64Encode(png)}}.dump();
    } catch (const std::exception& error) {
        std::cout << nlohmann::json{{"error", error.what()}}.dump();
    }
}
