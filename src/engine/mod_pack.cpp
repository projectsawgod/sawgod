#include "engine/mod_pack.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <miniz.h>

namespace sawgod {

namespace fs = std::filesystem;

namespace {

// `name = "..."` from mod.toml (the one field the engine needs), else "".
std::string toml_name(const std::vector<uint8_t> &data) {
    const std::string s(data.begin(), data.end());
    size_t at = 0;
    while ((at = s.find("name", at)) != std::string::npos) {
        const bool line_start = at == 0 || s[at - 1] == '\n';
        size_t i = at + 4;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        if (line_start && i < s.size() && s[i] == '=') {
            const size_t q = s.find('"', i);
            const size_t e = q == std::string::npos ? q : s.find('"', q + 1);
            if (e != std::string::npos && s.find('\n', i) > q)
                return s.substr(q + 1, e - q - 1);
        }
        at += 4;
    }
    return {};
}

class FolderMod : public ModPack {
  public:
    explicit FolderMod(const std::string &p) {
        path_ = p;
        name_ = fs::path(p).filename().string();
    }
    std::optional<std::vector<uint8_t>> read(const std::string &file) const override {
        std::ifstream in(fs::path(path_) / fs::path(file), std::ios::binary);
        if (!in)
            return std::nullopt;
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }
    std::vector<std::string> files() const override {
        std::vector<std::string> out;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(path_, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file())
                out.push_back(fs::relative(it->path(), path_).generic_string());
        std::sort(out.begin(), out.end());
        return out;
    }
};

class ZipMod : public ModPack {
    std::vector<uint8_t> data_; // the zip, read whole (mz_zip_reader_init_mem reads from it)

  public:
    ~ZipMod() override {
        if (open_)
            mz_zip_reader_end(&zip_);
    }
    bool open(const std::string &p, std::string *error) {
        path_ = p;
        name_ = fs::path(p).stem().string();
        // read whole, the file closed at once (Windows: a zip held open can't be replaced or deleted while
        // the game runs)
        std::ifstream in(fs::path(p), std::ios::binary);
        data_.assign(std::istreambuf_iterator<char>(in), {});
        if (!in.good() && !in.eof())
            data_.clear();
        if (data_.empty() || !mz_zip_reader_init_mem(&zip_, data_.data(), data_.size(), 0)) {
            if (error)
                *error = p + ": not a readable zip (" +
                         mz_zip_get_error_string(mz_zip_get_last_error(&zip_)) + ")";
            return false;
        }
        open_ = true;
        // zipping the mod's folder gives a zip with that folder at the top: look inside it
        const std::vector<std::string> all = files();
        if (!all.empty() && std::find(all.begin(), all.end(), "mod.toml") == all.end()) {
            const size_t slash = all[0].find('/');
            const std::string top = slash == std::string::npos ? std::string() : all[0].substr(0, slash + 1);
            // (only when that folder holds the mod -- mod.toml or a resource folder -- so a mod of just
            // tables/ isn't mistaken for one)
            const auto holds_mod = [&](const std::string &f) {
                for (const char *k : {"mod.toml", "tables/", "text/", "graphics/", "audio/"})
                    if (f.rfind(top + k, 0) == 0)
                        return true;
                return false;
            };
            if (!top.empty() &&
                std::all_of(all.begin(), all.end(),
                            [&](const std::string &f) { return f.rfind(top, 0) == 0; }) &&
                std::any_of(all.begin(), all.end(), holds_mod))
                prefix_ = top;
        }
        return true;
    }
    std::optional<std::vector<uint8_t>> read(const std::string &file) const override {
        auto *zip = const_cast<mz_zip_archive *>(&zip_);
        const int i = mz_zip_reader_locate_file(zip, (prefix_ + file).c_str(), nullptr, 0);
        if (i < 0)
            return std::nullopt;
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(zip, mz_uint(i), &size, 0);
        if (!p)
            return std::nullopt;
        std::vector<uint8_t> out(static_cast<uint8_t *>(p), static_cast<uint8_t *>(p) + size);
        mz_free(p);
        return out;
    }
    std::vector<std::string> files() const override {
        auto *zip = const_cast<mz_zip_archive *>(&zip_);
        std::vector<std::string> out;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(zip); ++i) {
            if (mz_zip_reader_is_file_a_directory(zip, i))
                continue;
            char name[1024];
            mz_zip_reader_get_filename(zip, i, name, sizeof name);
            std::string f = name;
            if (!prefix_.empty()) {
                if (f.rfind(prefix_, 0) != 0)
                    continue;
                f = f.substr(prefix_.size());
            }
            out.push_back(f);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

  private:
    mz_zip_archive zip_{};
    bool open_ = false;
    std::string prefix_;
};

} // namespace

std::unique_ptr<ModPack> ModPack::open(const std::string &path, std::string *error) {
    std::unique_ptr<ModPack> mod;
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        mod = std::make_unique<FolderMod>(path);
    } else {
        auto zip = std::make_unique<ZipMod>();
        if (!zip->open(path, error))
            return nullptr;
        mod = std::move(zip);
    }
    if (const auto toml = mod->read("mod.toml"))
        if (std::string n = toml_name(*toml); !n.empty())
            mod->name_ = n;
    return mod;
}

std::vector<std::unique_ptr<ModPack>> open_mods(const std::vector<std::string> &explicit_paths,
                                                const std::string &dir, std::vector<std::string> *errors) {
    std::vector<std::string> paths = explicit_paths;
    std::error_code ec;
    if (!dir.empty() && fs::is_directory(dir, ec)) {
        std::vector<std::string> found;
        for (const auto &e : fs::directory_iterator(dir, ec))
            if (e.is_directory() || e.path().extension() == ".sgz")
                found.push_back(e.path().string());
        std::sort(found.begin(), found.end());
        paths.insert(paths.end(), found.begin(), found.end());
    }
    std::vector<std::unique_ptr<ModPack>> mods;
    for (const std::string &p : paths) {
        std::string error;
        if (auto m = ModPack::open(p, &error))
            mods.push_back(std::move(m));
        else if (errors)
            errors->push_back(error);
    }
    return mods;
}

} // namespace sawgod
