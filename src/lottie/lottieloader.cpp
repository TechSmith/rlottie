/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
   #define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
   #define NOMINMAX
   #include <windows.h>
   #include <vector>
#endif

#include "lottiemodel.h"

using namespace rlottie::internal;

#ifdef LOTTIE_CACHE_SUPPORT

#include <mutex>
#include <unordered_map>

class ModelCache {
public:
    static ModelCache &instance()
    {
        static ModelCache singleton;
        return singleton;
    }
    std::shared_ptr<model::Composition> find(const std::string &key)
    {
        std::lock_guard<std::mutex> guard(mMutex);

        if (!mcacheSize) return nullptr;

        auto search = mHash.find(key);

        return (search != mHash.end()) ? search->second : nullptr;
    }
    void add(const std::string &key, std::shared_ptr<model::Composition> value)
    {
        std::lock_guard<std::mutex> guard(mMutex);

        if (!mcacheSize) return;

        //@TODO just remove the 1st element
        // not the best of LRU logic
        if (mcacheSize == mHash.size()) mHash.erase(mHash.cbegin());

        mHash[key] = std::move(value);
    }

    void configureCacheSize(size_t cacheSize)
    {
        std::lock_guard<std::mutex> guard(mMutex);
        mcacheSize = cacheSize;

        if (!mcacheSize) mHash.clear();
    }

private:
    ModelCache() = default;

    std::unordered_map<std::string, std::shared_ptr<model::Composition>> mHash;
    std::mutex                                                           mMutex;
    size_t mcacheSize{10};
};

#else

class ModelCache {
public:
    static ModelCache &instance()
    {
        static ModelCache singleton;
        return singleton;
    }
    std::shared_ptr<model::Composition> find(const std::string &)
    {
        return nullptr;
    }
    void add(const std::string &, std::shared_ptr<model::Composition>) {}
    void configureCacheSize(size_t) {}
};

#endif

static std::string dirname(const std::string &path)
{
    const char *ptr = strrchr(path.c_str(), '/');
#ifdef _WIN32
    if (ptr) ptr = strrchr(ptr + 1, '\\');
#endif
    int len = int(ptr + 1 - path.c_str());  // +1 to include '/'
    return std::string(path, 0, len);
}

void model::configureModelCacheSize(size_t cacheSize)
{
    ModelCache::instance().configureCacheSize(cacheSize);
}

namespace 
{
#ifdef _WIN32

   std::wstring fromUTF8(const char* src)
   {
       std::vector<wchar_t> buf(MultiByteToWideChar(CP_UTF8, 0, src, -1, 0, 0));
       MultiByteToWideChar(CP_UTF8, 0, src, -1, &buf[0], (int)buf.size());
       return &buf[0];
   }

   std::wstring decodeFilename(const char* utf8)
   {
       return fromUTF8(utf8);
   }

#else

   const char* decodeFilename(const char* utf8)
   {
       return utf8;
   }

#endif

   void fileOpen(std::ifstream &file, const char *filename,
                 std::ios_base::openmode mode)
   {
       file.open(decodeFilename(filename), mode);
   }
}

std::shared_ptr<model::Composition> model::loadFromFile(const std::string &path,
                                                        bool cachePolicy)
{
    if (cachePolicy) {
        auto obj = ModelCache::instance().find(path);
        if (obj) return obj;
    }

    std::ifstream f;
    fileOpen(f, path.c_str(), std::ios::in);

    if (!f.is_open()) {
        vCritical << "failed to open file = " << path.c_str();
        return {};
    } else {
        std::string content;
        f.seekg(0, std::ios::end);
        auto fsize = f.tellg();

        //read the given file
        content.reserve(fsize);
        f.seekg(0, std::ios::beg);
        content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        f.close();

        if (fsize == 0) return {};

        auto obj = internal::model::parse(const_cast<char *>(content.c_str()), fsize,
                                          dirname(path));

        if (obj && cachePolicy) ModelCache::instance().add(path, obj);

        return obj;
    }
}

std::shared_ptr<model::Composition> model::loadFromData(
    std::string jsonData, const std::string &key, std::string resourcePath,
    bool cachePolicy)
{
    if (cachePolicy) {
        auto obj = ModelCache::instance().find(key);
        if (obj) return obj;
    }

    auto obj = internal::model::parse(const_cast<char *>(jsonData.c_str()), jsonData.size(),
                                      std::move(resourcePath));

    if (obj && cachePolicy) ModelCache::instance().add(key, obj);

    return obj;
}

std::shared_ptr<model::Composition> model::loadFromData(
    std::string jsonData, std::string resourcePath, model::ColorFilter filter)
{
    return internal::model::parse(const_cast<char *>(jsonData.c_str()), jsonData.size(),
                                  std::move(resourcePath), std::move(filter));
}
