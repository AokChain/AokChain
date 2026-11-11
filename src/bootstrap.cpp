// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bootstrap.h"

#include "chainparams.h"
#include "chainparamsbase.h"
#include "clientversion.h"
#include "primitives/block.h"
#include "streams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "crypto/common.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "gunzip.h"
#include "init.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validation.h"

#include <univalue.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/thread.hpp>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

namespace {

const uint64_t MAX_MANIFEST_SIZE = 4 * 1024 * 1024;
const uint64_t MAX_CHUNK_SIZE = 8ULL * 1024 * 1024 * 1024;     // of one archive, decompressed
const size_t MAX_CHUNKS = 100000;
const int HTTP_TIMEOUT = 45;                // seconds without any data
const int DOWNLOAD_ATTEMPTS = 4;
const int CHUNKS_AHEAD = 2;                 // archives that wait on disk for their import, at most

struct Chunk {
    std::string file;
    int firstHeight;
    int lastHeight;
    uint256 lastHash;
    uint64_t size;              // decompressed
    uint64_t compressedSize;    // of the file as served
    bool gzip;
    std::string sha256;
};

CCriticalSection cs_bootstrap;
BootstrapInfo info;                         // guarded by cs_bootstrap
std::atomic<bool> fHold(false);             // peers are not asked for blocks
std::atomic<bool> fSkip(false);             // the user gave it up
std::atomic<bool> fStop(false);             // stop the download thread
int64_t nStartTime = 0;

void SetState(const std::string& state, const std::string& message)
{
    LOCK(cs_bootstrap);
    info.state = state;
    info.message = message;
}

bool Cancelled()
{
    return fSkip || fStop || ShutdownRequested();
}

// ---------------------------------------------------------------- HTTP

struct HttpJob {
    fs::path path;              // file to write; empty: collect the body in memory
    FILE* file;
    uint64_t offset;            // bytes of the file that are there already (resume)
    uint64_t received;
    uint64_t maxBytes;
    int status;                 // HTTP status, 0 when there was no reply
    bool finished;
    std::string body;
    std::string error;
    std::string location;       // of a redirect
    struct event_base* base;
    std::atomic<uint64_t>* progress;

    HttpJob() : file(nullptr), offset(0), received(0), maxBytes(0), status(0), finished(false), base(nullptr), progress(nullptr) {}
};

void HttpChunkCallback(struct evhttp_request* req, void* ctx)
{
    HttpJob* job = static_cast<HttpJob*>(ctx);
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
    const int code = evhttp_request_get_response_code(req);
    if (code != 200 && code != 206) {
        evbuffer_drain(buf, evbuffer_get_length(buf));
        return;
    }
    if (code == 200 && job->offset > 0) {
        // the server does not do ranges and sends the whole file: start the file again
        if (job->file)
            fclose(job->file);
        job->file = fsbridge::fopen(job->path, "wb");
        if (job->progress)
            *job->progress -= job->offset;
        job->offset = 0;
        if (!job->file) {
            job->error = "cannot write " + job->path.string();
            event_base_loopbreak(job->base);
            return;
        }
    }
    char data[65536];
    int len;
    while ((len = evbuffer_remove(buf, data, sizeof(data))) > 0) {
        job->received += len;
        if (job->offset + job->received > job->maxBytes) {
            job->error = "the server sends more than the manifest says";
            event_base_loopbreak(job->base);
            return;
        }
        if (job->file) {
            if (fwrite(data, 1, len, job->file) != (size_t)len) {
                job->error = "cannot write " + job->path.string() + " (disk full?)";
                event_base_loopbreak(job->base);
                return;
            }
        } else {
            job->body.append(data, len);
        }
        if (job->progress)
            *job->progress += len;
    }
}

void HttpDoneCallback(struct evhttp_request* req, void* ctx)
{
    HttpJob* job = static_cast<HttpJob*>(ctx);
    if (req) {
        job->status = evhttp_request_get_response_code(req);
        job->finished = true;
        const char* location = evhttp_find_header(evhttp_request_get_input_headers(req), "Location");
        if (location)
            job->location = location;
    }
    event_base_loopexit(job->base, nullptr);
}

void HttpTickCallback(evutil_socket_t, short, void* ctx)
{
    HttpJob* job = static_cast<HttpJob*>(ctx);
    if (Cancelled()) {
        job->error = "cancelled";
        event_base_loopbreak(job->base);
    }
}

/**
 * GET url. With a path the body is appended to that file, after the bytes that are there (a Range
 * request); without, it is returned in body. False with an error when the transfer did not complete.
 */
bool HttpGetOnce(const std::string& url, const fs::path& path, uint64_t maxBytes, std::atomic<uint64_t>* progress,
                 std::string* body, std::string& error, std::string& redirect)
{
    struct evhttp_uri* uri = evhttp_uri_parse(url.c_str());
    if (!uri) {
        error = "invalid address " + url;
        return false;
    }
    const char* scheme = evhttp_uri_get_scheme(uri);
    const char* host = evhttp_uri_get_host(uri);
    if (!scheme || std::string(scheme) != "http" || !host) {
        evhttp_uri_free(uri);
        error = "this program fetches http:// addresses only (the GUI wallet also does https://; or fetch the files yourself and use -bootstrapdir): " + url;
        return false;
    }
    const int port = evhttp_uri_get_port(uri) > 0 ? evhttp_uri_get_port(uri) : 80;
    std::string target = evhttp_uri_get_path(uri) && *evhttp_uri_get_path(uri) ? evhttp_uri_get_path(uri) : "/";
    if (evhttp_uri_get_query(uri))
        target += std::string("?") + evhttp_uri_get_query(uri);
    const std::string hostName = host;
    evhttp_uri_free(uri);

    HttpJob job;
    job.path = path;
    job.maxBytes = maxBytes;
    job.progress = progress;
    if (!path.empty()) {
        boost::system::error_code ec;
        if (fs::exists(path, ec))
            job.offset = fs::file_size(path, ec);
        if (job.offset >= maxBytes) {
            // nothing sensible to resume: start again
            job.offset = 0;
            fs::remove(path, ec);
        }
        job.file = fsbridge::fopen(path, job.offset > 0 ? "ab" : "wb");
        if (!job.file) {
            error = "cannot write " + path.string();
            return false;
        }
        if (progress)
            *progress += job.offset;
    }

    job.base = event_base_new();
    struct evhttp_connection* conn = evhttp_connection_base_new(job.base, nullptr, hostName.c_str(), port);
    struct evhttp_request* req = evhttp_request_new(HttpDoneCallback, &job);
    struct event* tick = event_new(job.base, -1, EV_PERSIST, HttpTickCallback, &job);
    bool started = false;
    if (conn && req && tick) {
        evhttp_connection_set_timeout(conn, HTTP_TIMEOUT);
        evhttp_request_set_chunked_cb(req, HttpChunkCallback);
        struct evkeyvalq* headers = evhttp_request_get_output_headers(req);
        evhttp_add_header(headers, "Host", (port == 80 ? hostName : hostName + ":" + itostr(port)).c_str());
        evhttp_add_header(headers, "Connection", "close");
        evhttp_add_header(headers, "User-Agent", "aokchain-bootstrap");
        if (job.offset > 0)
            evhttp_add_header(headers, "Range", strprintf("bytes=%u-", job.offset).c_str());
        struct timeval interval = {0, 300000};
        event_add(tick, &interval);
        started = evhttp_make_request(conn, req, EVHTTP_REQ_GET, target.c_str()) == 0;
    }
    if (started)
        event_base_dispatch(job.base);  // the request belongs to the connection from here on
    else if (req && !conn)
        evhttp_request_free(req);
    if (tick)
        event_free(tick);
    if (conn)
        evhttp_connection_free(conn);
    event_base_free(job.base);
    if (job.file)
        fclose(job.file);

    if (!job.error.empty()) {
        error = job.error;
        return false;
    }
    if (!started || !job.finished) {
        error = "no answer from " + hostName;
        return false;
    }
    if (job.status == 416 && job.offset > 0) {
        // the range starts after the end: what is on disk is not a part of this file
        boost::system::error_code ec;
        fs::remove(path, ec);
        if (progress)
            *progress -= job.offset;
        error = "the partial file did not fit; starting again";
        return false;
    }
    if ((job.status == 301 || job.status == 302 || job.status == 303 || job.status == 307 || job.status == 308) && !job.location.empty()) {
        if (progress)
            *progress -= job.offset;
        redirect = job.location;
        return false;
    }
    if (job.status != 200 && job.status != 206) {
        error = strprintf("HTTP status %d for %s", job.status, url);
        return false;
    }
    if (body)
        *body = job.body;
    return true;
}

/** The fetcher of the daemon: plain HTTP with libevent, following a few redirects. */
bool HttpGet(const std::string& url, const std::string& pathString, uint64_t maxBytes, std::atomic<uint64_t>* progress,
             std::string* body, std::string& error)
{
    const fs::path path(pathString);
    std::string current = url;
    for (int hop = 0; hop < 5; ++hop) {
        std::string redirect;
        if (HttpGetOnce(current, path, maxBytes, progress, body, error, redirect))
            return true;
        if (redirect.empty())
            return false;
        if (redirect.compare(0, 7, "http://") != 0) {
            error = "redirected to an address that is not http://: " + redirect;
            return false;
        }
        current = redirect;
    }
    error = "too many redirects";
    return false;
}

BootstrapFetcher fetcher = HttpGet;
bool fFetcherHttps = false;     // the fetcher can do https:// addresses

// ---------------------------------------------------------------- manifest

bool IsSafeFileName(const std::string& name)
{
    if (name.empty() || name.size() > 100 || name[0] == '.')
        return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return false;
    }
    return true;
}

bool IsHash(const std::string& hex)
{
    return hex.size() == 64 && IsHex(hex);
}

bool ParseManifest(const std::string& text, std::vector<Chunk>& chunks, std::string& error)
{
    UniValue manifest;
    if (!manifest.read(text) || !manifest.isObject()) {
        error = "the manifest is not JSON";
        return false;
    }
    try {
        if (find_value(manifest, "format").get_int() != 1) {
            error = "the manifest has a format that this version does not know";
            return false;
        }
        if (find_value(manifest, "network").get_str() != Params().NetworkIDString()) {
            error = "the archives are for another network";
            return false;
        }
        const UniValue& list = find_value(manifest, "chunks").get_array();
        if (list.size() == 0 || list.size() > MAX_CHUNKS) {
            error = "the manifest lists no archives";
            return false;
        }
        int next = -1;
        for (size_t i = 0; i < list.size(); ++i) {
            const UniValue& item = list[i];
            Chunk chunk;
            chunk.file = find_value(item, "file").get_str();
            chunk.firstHeight = find_value(item, "first_height").get_int();
            chunk.lastHeight = find_value(item, "last_height").get_int();
            const std::string lastHash = find_value(item, "last_hash").get_str();
            chunk.size = (uint64_t)find_value(item, "size").get_int64();
            chunk.compressedSize = (uint64_t)find_value(item, "compressed_size").get_int64();
            const std::string compression = find_value(item, "compression").get_str();
            chunk.sha256 = find_value(item, "sha256").get_str();
            if (!IsSafeFileName(chunk.file) || !IsHash(lastHash) || !IsHash(chunk.sha256)
                || (compression != "gzip" && compression != "none")
                || chunk.firstHeight < 1 || chunk.lastHeight < chunk.firstHeight
                || (next != -1 && chunk.firstHeight != next)
                || chunk.size == 0 || chunk.size > MAX_CHUNK_SIZE || chunk.compressedSize == 0 || chunk.compressedSize > MAX_CHUNK_SIZE) {
                error = strprintf("entry %u of the manifest is not valid", i);
                return false;
            }
            chunk.lastHash = uint256S(lastHash);
            chunk.gzip = compression == "gzip";
            for (char& c : chunk.sha256)
                c = (char)tolower((unsigned char)c);
            next = chunk.lastHeight + 1;
            chunks.push_back(chunk);
        }
    } catch (const std::exception&) {
        error = "the manifest lacks a field";
        return false;
    }
    return true;
}

bool FileSha256(const fs::path& path, std::string& hex)
{
    FILE* file = fsbridge::fopen(path, "rb");
    if (!file)
        return false;
    CSHA256 hasher;
    std::vector<unsigned char> buffer(1 << 20);
    size_t n;
    while ((n = fread(buffer.data(), 1, buffer.size(), file)) > 0)
        hasher.Write(buffer.data(), n);
    fclose(file);
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(hash);
    hex = HexStr(hash, hash + sizeof(hash));
    return true;
}

// ---------------------------------------------------------------- download thread

struct Shared {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<int> downloaded;    // archives that are complete on disk and checked
    std::atomic<int> imported;      // archives whose import is finished (their files are gone)
    std::string error;          // the download gave up
    std::atomic<uint64_t> bytes;
    std::atomic<int64_t> downloadMillis;

    Shared() : downloaded(0), imported(0), bytes(0), downloadMillis(0) {}
};

/** The transfer that is running, for GetBootstrapInfo(); guarded by cs_bootstrap. */
Shared* pshared = nullptr;

struct SharedGuard {
    explicit SharedGuard(Shared* shared) { LOCK(cs_bootstrap); pshared = shared; }
    ~SharedGuard() { LOCK(cs_bootstrap); pshared = nullptr; }
};

/**
 * Fetch one archive to dir/file and check its size and SHA-256. A partial file is resumed. The
 * mirrors are tried in turn, beginning with the one that worked last; a mirror that fails, stalls
 * or serves a file with another hash is left for the next one.
 */
bool DownloadChunk(const std::vector<std::string>& mirrors, size_t& mirror, const fs::path& dir, const Chunk& chunk, Shared& shared, std::string& error)
{
    const fs::path path = dir / chunk.file;
    const fs::path part = dir / (chunk.file + ".part");
    boost::system::error_code ec;
    const uint64_t before = shared.bytes;
    for (size_t tried = 0; tried < mirrors.size() && !Cancelled(); ++tried) {
        const std::string& baseUrl = mirrors[(mirror + tried) % mirrors.size()];
        for (int attempt = 0; attempt < DOWNLOAD_ATTEMPTS && !Cancelled(); ++attempt) {
            shared.bytes = before;
            if (!fs::exists(path, ec)) {
                const int64_t start = GetTimeMillis();
                std::string httpError;
                const bool got = fetcher(baseUrl + chunk.file, part.string(), chunk.compressedSize, &shared.bytes, nullptr, httpError);
                shared.downloadMillis += GetTimeMillis() - start;
                if (!got || fs::file_size(part, ec) != chunk.compressedSize) {
                    error = got ? "the file is shorter than the manifest says" : httpError;
                    LogPrintf("Bootstrap: %s from %s: %s (attempt %d)\n", chunk.file, baseUrl, error, attempt + 1);
                    if (!Cancelled())
                        MilliSleep(500 * (attempt + 1));
                    continue; // what arrived stays for the next attempt
                }
                fs::rename(part, path, ec);
            }
            std::string hash;
            if (FileSha256(path, hash) && fs::file_size(path, ec) == chunk.compressedSize && hash == chunk.sha256) {
                shared.bytes = before + chunk.compressedSize;
                mirror = (mirror + tried) % mirrors.size();
                return true;
            }
            // not the file of the manifest: nothing of it is kept, and this mirror is not asked again for it
            fs::remove(path, ec);
            error = "the SHA-256 of the file is not that of the manifest";
            LogPrintf("Bootstrap: %s from %s: %s\n", chunk.file, baseUrl, error);
            break;
        }
    }
    shared.bytes = before;
    if (error.empty())
        error = "cancelled";
    return false;
}

void DownloadThread(std::vector<std::string> mirrors, size_t mirror, fs::path dir, std::vector<Chunk> chunks, Shared* shared)
{
    RenameThread("aokchain-bootdl");
    for (size_t i = 0; i < chunks.size(); ++i) {
        {
            // not too far ahead of the import: the files are large
            std::unique_lock<std::mutex> lock(shared->mutex);
            while (!Cancelled() && shared->downloaded - shared->imported >= CHUNKS_AHEAD)
                shared->changed.wait_for(lock, std::chrono::milliseconds(200));
        }
        if (Cancelled())
            return;
        std::string error;
        const bool ok = DownloadChunk(mirrors, mirror, dir, chunks[i], *shared, error);
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!ok) {
            shared->error = strprintf("%s: %s", chunks[i].file, error);
            shared->changed.notify_all();
            return;
        }
        shared->downloaded = i + 1;
        shared->changed.notify_all();
        LogPrintf("Bootstrap: downloaded %s (%d/%d)\n", chunks[i].file, i + 1, chunks.size());
    }
}

// ---------------------------------------------------------------- import

/**
 * Import the blocks of an archive, one after the other, in the order of the file.
 *
 * Every block goes through ProcessNewBlock(), the call that a block from a peer goes through:
 * CheckBlock, AcceptBlock, and ActivateBestChain with ConnectBlock. A block is handed over only
 * when the block before it is connected. That matters: some checks of AcceptBlock (the fee of a
 * token issue) read the state of the chain tip, so a block that is accepted far ahead of the tip,
 * as a file import with LoadExternalBlockFile() does, can be judged by an outdated state.
 */
bool ImportChunk(const fs::path& dir, const fs::path& source, const Chunk& chunk, std::string& error)
{
    const CChainParams& chainparams = Params();
    fs::path plain = source;
    boost::system::error_code ec;
    if (chunk.gzip) {
        plain = dir / (chunk.file + ".plain");
        FILE* in = fsbridge::fopen(source, "rb");
        FILE* out = fsbridge::fopen(plain, "wb");
        uint64_t size = 0;
        std::string gzipError = "cannot open the files";
        const bool ok = in && out && GunzipFile(in, out, chunk.size, size, gzipError);
        if (in)
            fclose(in);
        if (out)
            fclose(out);
        if (!ok || size != chunk.size) {
            fs::remove(plain, ec);
            error = "cannot decompress " + chunk.file + ": " + (ok ? "its length is not that of the manifest" : gzipError);
            return false;
        }
    }

    FILE* file = fsbridge::fopen(plain, "rb");
    if (!file) {
        error = "cannot open " + plain.string();
        return false;
    }
    const unsigned char* magic = chainparams.MessageStart();
    std::vector<char> raw;
    bool ok = true;
    int height = chunk.firstHeight;
    while (ok) {
        if (height % 256 == 0) {
            boost::this_thread::interruption_point();
            if (Cancelled()) {
                error = "cancelled";
                ok = false;
                break;
            }
        }
        unsigned char header[8];
        const size_t got = fread(header, 1, 8, file);
        if (got == 0)
            break; // the end of the archive
        const uint32_t length = ReadLE32(header + 4);
        if (got != 8 || memcmp(header, magic, 4) != 0 || length < 80 || length > MAX_BLOCK_SERIALIZED_SIZE) {
            error = chunk.file + " is not a sequence of blocks";
            ok = false;
            break;
        }
        raw.resize(length);
        if (fread(raw.data(), 1, length, file) != length) {
            error = chunk.file + " ends in the middle of a block";
            ok = false;
            break;
        }
        std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
        try {
            CDataStream stream(raw.data(), raw.data() + length, SER_DISK, CLIENT_VERSION);
            stream >> *pblock;
        } catch (const std::exception&) {
            error = strprintf("%s: the block at height %d cannot be read", chunk.file, height);
            ok = false;
            break;
        }
        const uint256 hash = pblock->GetBlockHash();
        bool have = false;
        {
            // a block that is part of the chain already (a bootstrap that goes on after a restart)
            LOCK(cs_main);
            BlockMap::const_iterator it = mapBlockIndex.find(hash);
            have = it != mapBlockIndex.end() && chainActive.Contains(it->second);
        }
        if (!have) {
            bool fNewBlock = false;
            ProcessNewBlock(chainparams, pblock, true, &fNewBlock, hash);
            LOCK(cs_main);
            BlockMap::const_iterator it = mapBlockIndex.find(hash);
            if (it == mapBlockIndex.end() || !chainActive.Contains(it->second)) {
                error = strprintf("the block at height %d of %s was not accepted: the chain stops at height %d", height, chunk.file, chainActive.Height());
                ok = false;
                break;
            }
        }
        ++height;
    }
    fclose(file);
    if (chunk.gzip)
        fs::remove(plain, ec);
    if (!ok)
        return false;
    if (height - 1 != chunk.lastHeight) {
        error = strprintf("%s has %d blocks, the manifest says %d", chunk.file, height - chunk.firstHeight, chunk.lastHeight - chunk.firstHeight + 1);
        return false;
    }
    // the chain of this node must now contain the last block of the archive
    LOCK(cs_main);
    const CBlockIndex* pindex = chainActive[chunk.lastHeight];
    if (!pindex || pindex->GetBlockHash() != chunk.lastHash) {
        error = strprintf("after %s the chain does not have block %s at height %d", chunk.file, chunk.lastHash.GetHex(), chunk.lastHeight);
        return false;
    }
    return true;
}

void Finish(const std::string& state, const std::string& message, const fs::path& dir)
{
    // nothing large stays on disk
    boost::system::error_code ec;
    fs::remove_all(dir, ec);
    fHold = false;
    {
        LOCK(cs_bootstrap);
        info.state = state;
        info.message = message;
        info.active = false;
        info.secondsTotal = GetTime() - nStartTime;
    }
    LogPrintf("Bootstrap: %s%s%s\n", state, message.empty() ? "" : ": ", message);
}

/** Read manifest.json of a folder. */
bool ReadLocalManifest(const fs::path& folder, std::string& text, std::string& error)
{
    FILE* file = fsbridge::fopen(folder / "manifest.json", "rb");
    if (!file) {
        error = "there is no manifest.json in " + folder.string();
        return false;
    }
    char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0 && text.size() <= MAX_MANIFEST_SIZE)
        text.append(buffer, n);
    fclose(file);
    return true;
}

/**
 * The bootstrap, from a folder of this computer when one is given (the archives were fetched by
 * other means: a disk, a file share, a torrent), from the mirrors otherwise.
 */
void Run(const std::vector<std::string>& mirrors, const fs::path& folder)
{
    const fs::path dir = GetDataDir() / "bootstrap";
    const bool local = !folder.empty();
    // the block index must be ready, and a reindex or -loadblock must be over
    while (fImporting || fReindex) {
        if (ShutdownRequested())
            return;
        MilliSleep(200);
    }
    if (fSkip) {
        Finish("skipped", "", dir);
        return;
    }

    // A node that is nearly synchronized asks no server for anything (unless a bootstrap was
    // interrupted and its files wait to be used).
    boost::system::error_code existsError;
    if (!local && !fs::exists(dir, existsError)) {
        int64_t tipTime;
        {
            LOCK(cs_main);
            tipTime = chainActive.Tip() ? chainActive.Tip()->GetBlockTime() : 0;
        }
        if (tipTime > GetTime() - BOOTSTRAP_MIN_TIP_AGE) {
            Finish("notneeded", "the chain of this node is recent", dir);
            return;
        }
    }

    SetState("manifest", "reading the list of archives");
    std::string text, error;
    std::vector<Chunk> all;
    size_t mirror = 0;
    bool have = false;
    if (local) {
        have = ReadLocalManifest(folder, text, error);
    } else {
        // the manifest of the first mirror that answers
        for (; mirror < mirrors.size() && !have && !Cancelled(); ++mirror) {
            text.clear();
            have = fetcher(mirrors[mirror] + "manifest.json", std::string(), MAX_MANIFEST_SIZE, nullptr, &text, error);
            if (have)
                break;
            LogPrintf("Bootstrap: %s: %s\n", mirrors[mirror], error);
        }
    }
    if (!have || !ParseManifest(text, all, error)) {
        Finish(fSkip ? "skipped" : "failed", error, dir);
        return;
    }

    int height;
    {
        LOCK(cs_main);
        height = chainActive.Height();
    }
    const int target = all.back().lastHeight;
    std::vector<Chunk> chunks;
    uint64_t bytesTotal = 0;
    for (const Chunk& chunk : all) {
        if (chunk.lastHeight > height) {
            chunks.push_back(chunk);
            bytesTotal += chunk.compressedSize;
        }
    }
    // a folder that the user chose is used whenever it has blocks that the node lacks
    const int minBlocks = local ? 1 : gArgs.GetArg("-bootstrapminblocks", DEFAULT_BOOTSTRAP_MIN_BLOCKS);
    if (chunks.empty() || target - height < minBlocks) {
        Finish("notneeded", strprintf("the node is at height %d, the archives end at %d", height, target), dir);
        return;
    }

    boost::system::error_code ec;
    fs::create_directories(dir, ec);
    {
        LOCK(cs_bootstrap);
        info.state = "running";
        info.message = "";
        info.chunks = chunks.size();
        info.bytesTotal = bytesTotal;
        info.startHeight = height;
        info.targetHeight = target;
    }
    LogPrintf("Bootstrap: %d archive(s), %.1f MB, blocks %d to %d, from %s\n", chunks.size(), bytesTotal / 1e6,
              chunks.front().firstHeight, target, local ? folder.string() : mirrors[mirror]);

    Shared shared;
    SharedGuard guard(&shared);
    std::thread downloader;
    if (!local)
        downloader = std::thread(DownloadThread, mirrors, mirror, dir, chunks, &shared);
    std::string failure;
    int64_t importMillis = 0;
    try {
        for (size_t i = 0; i < chunks.size() && failure.empty(); ++i) {
            fs::path source = dir / chunks[i].file;
            if (local) {
                // the file of the folder, checked against the manifest; it is only read
                source = folder / chunks[i].file;
                const int64_t start = GetTimeMillis();
                std::string hash;
                if (!FileSha256(source, hash))
                    failure = chunks[i].file + " is missing in the folder";
                else if (hash != chunks[i].sha256)
                    failure = "the SHA-256 of " + chunks[i].file + " is not that of the manifest";
                shared.downloadMillis += GetTimeMillis() - start;
                if (failure.empty()) {
                    shared.bytes += chunks[i].compressedSize;
                    shared.downloaded = i + 1;
                }
            }
            // wait for the archive
            while (!local) {
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    if (shared.downloaded > (int)i)
                        break;
                    if (!shared.error.empty()) {
                        failure = shared.error;
                        break;
                    }
                }
                if (Cancelled()) {
                    failure = "cancelled";
                    break;
                }
                {
                    LOCK(cs_bootstrap);
                    info.bytesDownloaded = shared.bytes;
                    info.chunksDownloaded = shared.downloaded;
                    info.secondsDownload = shared.downloadMillis / 1000;
                    info.secondsTotal = GetTime() - nStartTime;
                }
                MilliSleep(200);
            }
            if (!failure.empty())
                break;
            {
                LOCK(cs_bootstrap);
                info.bytesDownloaded = shared.bytes;
                info.chunksDownloaded = shared.downloaded;
            }
            const int64_t start = GetTimeMillis();
            std::string importError;
            const bool ok = ImportChunk(dir, source, chunks[i], importError);
            importMillis += GetTimeMillis() - start;
            if (!local)
                fs::remove(source, ec);
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.imported = i + 1;
                shared.changed.notify_all();
            }
            {
                LOCK(cs_bootstrap);
                info.chunksImported = ok ? i + 1 : i;
                info.chunksDownloaded = shared.downloaded;
                info.bytesDownloaded = shared.bytes;
                info.secondsImport = importMillis / 1000;
                info.secondsDownload = shared.downloadMillis / 1000;
                info.secondsTotal = GetTime() - nStartTime;
            }
            if (!ok)
                failure = importError;
            else
                LogPrintf("Bootstrap: imported %s (%d/%d) in %.1f s, height %d\n", chunks[i].file, i + 1, chunks.size(),
                          (GetTimeMillis() - start) / 1000.0, chunks[i].lastHeight);
        }
    } catch (const boost::thread_interrupted&) {
        // shutdown: the files stay, the next start goes on from here
        fStop = true;
        if (downloader.joinable())
            downloader.join();
        fHold = false;
        throw;
    }
    fStop = true;
    if (downloader.joinable())
        downloader.join();
    if (ShutdownRequested() && !failure.empty()) {
        fHold = false;
        return; // keep the files for the next start
    }
    {
        LOCK(cs_bootstrap);
        info.secondsDownload = shared.downloadMillis / 1000;
        info.secondsImport = importMillis / 1000;
    }
    if (failure.empty())
        Finish("done", strprintf("%d archive(s), %.1f MB, %s %d s, import %d s", chunks.size(), bytesTotal / 1e6,
                                 local ? "check" : "download", shared.downloadMillis / 1000, importMillis / 1000), dir);
    else if (fSkip)
        Finish("skipped", "", dir);
    else
        Finish("failed", failure, dir);
}

std::atomic<bool> fRunning(false);
boost::thread_group* pthreadGroup = nullptr;

void ThreadBootstrap(std::vector<std::string> mirrors, std::string folder)
{
    RenameThread("aokchain-bootstrap");
    try {
        // In a new data directory the import thread connects the genesis block: no block is
        // handed to validation before the chain has a tip.
        while (!Cancelled()) {
            {
                LOCK(cs_main);
                if (chainActive.Tip())
                    break;
            }
            MilliSleep(50);
        }
        Run(mirrors, fs::path(folder));
    } catch (const boost::thread_interrupted&) {
        fHold = false;
        fRunning = false;
        throw;
    } catch (const std::exception& e) {
        fHold = false;
        SetState("failed", e.what());
        LogPrintf("Bootstrap: failed: %s\n", e.what());
    }
    fRunning = false;
}

/** Begin a bootstrap. False when one is running already. */
bool Launch(const std::vector<std::string>& mirrors, const std::string& folder)
{
    if (!pthreadGroup || fRunning.exchange(true))
        return false;
    {
        LOCK(cs_bootstrap);
        const std::string url = info.url;
        info = BootstrapInfo();
        info.url = folder.empty() ? url : folder;
        info.configured = true;
        info.state = "starting";
        info.active = true;
    }
    nStartTime = GetTime();
    fSkip = false;
    fStop = false;
    // from now on the peers are asked for headers only, until the bootstrap is over
    fHold = true;
    pthreadGroup->create_thread(boost::bind(&ThreadBootstrap, mirrors, folder));
    return true;
}

} // namespace

namespace {

/** The mirrors and the folder that the options (or the built-in address) name. */
void GetSources(std::vector<std::string>& mirrors, std::string& folder, bool log)
{
    mirrors.clear();
    for (const std::string& value : gArgs.GetArgs("-bootstrapurl")) {
        // one value may hold several addresses, separated by white space or commas (the settings page does that)
        std::vector<std::string> urls;
        boost::split(urls, value, boost::is_any_of(" \t\r\n,;"), boost::token_compress_on);
        for (std::string url : urls) {
            if (url.empty())
                continue;
            if (url[url.size() - 1] != '/')
                url += "/";
            mirrors.push_back(url);
        }
    }
    folder = gArgs.GetArg("-bootstrapdir", "");
    if (mirrors.empty() && folder.empty() && Params().NetworkIDString() == CBaseChainParams::MAIN) {
        // The built-in address of the main network. It is an https:// address, which the GUI can
        // fetch and the daemon cannot: the daemon then goes on without it.
        if (fFetcherHttps) {
            mirrors.push_back(DEFAULT_BOOTSTRAP_URL_MAIN);
        } else if (log) {
            LogPrintf("Bootstrap: off. The files at %s need https, which this program does not do. "
                      "To use them, fetch the files into a folder and start with -bootstrapdir=<folder>, or give an http:// mirror with -bootstrapurl.\n",
                      DEFAULT_BOOTSTRAP_URL_MAIN);
        }
    }
}

} // namespace

bool BootstrapHasSource()
{
    std::vector<std::string> mirrors;
    std::string folder;
    GetSources(mirrors, folder, false);
    return !mirrors.empty() || !folder.empty();
}

bool BootstrapReadFile(const std::string& name, std::string& text, std::string& error)
{
    std::vector<std::string> mirrors;
    std::string folder;
    GetSources(mirrors, folder, false);
    if (!folder.empty()) {
        FILE* file = fsbridge::fopen(fs::path(folder) / name, "rb");
        if (!file) {
            error = "there is no " + name + " in " + folder;
            return false;
        }
        char buffer[65536];
        size_t n;
        text.clear();
        while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0 && text.size() <= MAX_MANIFEST_SIZE)
            text.append(buffer, n);
        fclose(file);
        return true;
    }
    for (const std::string& mirror : mirrors) {
        text.clear();
        if (fetcher(mirror + name, std::string(), MAX_MANIFEST_SIZE, nullptr, &text, error))
            return true;
        LogPrintf("Bootstrap: %s%s: %s\n", mirror, name, error);
    }
    if (error.empty())
        error = "no source for " + name;
    return false;
}

bool BootstrapGetFile(const std::string& name, uint64_t size, const std::string& sha256, const std::string& dir,
                      std::atomic<uint64_t>* progress, std::string& path, bool& temporary, std::string& error)
{
    if (!fHold) {
        // no block bootstrap is running: its stop marks do not concern this transfer
        fStop = false;
        fSkip = false;
    }
    if (!IsSafeFileName(name)) {
        error = "not a file name: " + name;
        return false;
    }
    std::vector<std::string> mirrors;
    std::string folder;
    GetSources(mirrors, folder, false);
    if (!folder.empty()) {
        const fs::path source = fs::path(folder) / name;
        std::string hash;
        if (!FileSha256(source, hash)) {
            error = name + " is missing in the folder";
            return false;
        }
        if (hash != sha256) {
            error = "the SHA-256 of " + name + " is not that of the manifest";
            return false;
        }
        if (progress)
            *progress += size;
        path = source.string();
        temporary = false;
        return true;
    }
    Chunk chunk;
    chunk.file = name;
    chunk.compressedSize = size;
    chunk.sha256 = sha256;
    Shared shared;
    if (progress)
        shared.bytes = progress->load();
    size_t mirror = 0;
    boost::system::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    // the bytes are reported while the file arrives
    std::atomic<bool> done(false);
    std::thread reporter;
    if (progress) {
        reporter = std::thread([&] {
            while (!done) {
                *progress = shared.bytes.load();
                MilliSleep(200);
            }
        });
    }
    const bool ok = !mirrors.empty() && DownloadChunk(mirrors, mirror, fs::path(dir), chunk, shared, error);
    done = true;
    if (reporter.joinable())
        reporter.join();
    if (progress)
        *progress = shared.bytes.load();
    if (!ok) {
        if (mirrors.empty())
            error = "no address to fetch " + name + " from";
        return false;
    }
    path = (fs::path(dir) / name).string();
    temporary = true;
    return true;
}

bool BootstrapVisitBlocks(int firstHeight, int lastHeight,
                          const std::function<bool(int height, const std::vector<char>& block, std::string& error)>& visit,
                          const std::function<bool(int lastHeightOfArchive, std::string& error)>& archiveDone,
                          std::atomic<uint64_t>* bytesDone, std::atomic<uint64_t>* bytesTotal, std::string& error)
{
    if (!fHold) {
        // no block bootstrap is running: its stop marks do not concern this transfer
        fStop = false;
        fSkip = false;
    }
    std::string text;
    std::vector<Chunk> all;
    if (!BootstrapReadFile("manifest.json", text, error) || !ParseManifest(text, all, error))
        return false;
    if (all.back().lastHeight < lastHeight) {
        error = strprintf("the block archives end at height %d, the blocks up to %d are needed", all.back().lastHeight, lastHeight);
        return false;
    }
    uint64_t total = 0;
    for (const Chunk& chunk : all) {
        if (chunk.firstHeight <= lastHeight)
            total += chunk.compressedSize;
    }
    if (bytesTotal)
        *bytesTotal = total;
    const fs::path dir = GetDataDir() / "pastblocks";
    const unsigned char* magic = Params().MessageStart();
    boost::system::error_code ec;
    bool ok = true;
    for (size_t i = 0; i < all.size() && ok && all[i].firstHeight <= lastHeight; ++i) {
        const Chunk& chunk = all[i];
        if (chunk.lastHeight < firstHeight) {
            // done by an earlier visit
            if (bytesDone)
                *bytesDone += chunk.compressedSize;
            continue;
        }
        std::string path;
        bool temporary = false;
        if (!BootstrapGetFile(chunk.file, chunk.compressedSize, chunk.sha256, dir.string(), bytesDone, path, temporary, error))
            return false;
        // decompress next to the download, then read the blocks from there
        fs::create_directories(dir, ec);
        fs::path plain = fs::path(path);
        if (chunk.gzip) {
            plain = dir / (chunk.file + ".plain");
            FILE* in = fsbridge::fopen(fs::path(path), "rb");
            FILE* out = fsbridge::fopen(plain, "wb");
            uint64_t size = 0;
            std::string gzipError = "cannot open the files";
            const bool unpacked = in && out && GunzipFile(in, out, chunk.size, size, gzipError);
            if (in)
                fclose(in);
            if (out)
                fclose(out);
            if (!unpacked || size != chunk.size) {
                error = "cannot decompress " + chunk.file + ": " + gzipError;
                ok = false;
            }
        }
        FILE* file = ok ? fsbridge::fopen(plain, "rb") : nullptr;
        if (ok && !file) {
            error = "cannot open " + plain.string();
            ok = false;
        }
        std::vector<char> raw;
        int height = chunk.firstHeight;
        while (ok && height <= lastHeight) {
            if (height % 256 == 0 && Cancelled()) {
                error = "cancelled";
                ok = false;
                break;
            }
            unsigned char header[8];
            const size_t got = fread(header, 1, 8, file);
            if (got == 0)
                break;
            const uint32_t length = ReadLE32(header + 4);
            if (got != 8 || memcmp(header, magic, 4) != 0 || length < 80 || length > MAX_BLOCK_SERIALIZED_SIZE) {
                error = chunk.file + " is not a sequence of blocks";
                ok = false;
                break;
            }
            raw.resize(length);
            if (fread(raw.data(), 1, length, file) != length) {
                error = chunk.file + " ends in the middle of a block";
                ok = false;
                break;
            }
            if (!visit(height, raw, error))
                ok = false;
            ++height;
        }
        if (file)
            fclose(file);
        if (chunk.gzip)
            fs::remove(plain, ec);
        if (temporary)
            fs::remove(fs::path(path), ec);
        if (ok && height <= std::min(lastHeight, chunk.lastHeight)) {
            error = chunk.file + " has fewer blocks than the manifest says";
            ok = false;
        }
        if (ok && archiveDone && !archiveDone(height - 1, error))
            ok = false;
    }
    if (ok)
        fs::remove_all(dir, ec);
    return ok;
}

void StartBootstrap(boost::thread_group& threadGroup)
{
    pthreadGroup = &threadGroup;
    // -bootstrapurl can be given several times: mirrors, tried in this order
    std::vector<std::string> mirrors;
    std::string folder;
    const bool enabled = gArgs.GetBoolArg("-bootstrap", DEFAULT_BOOTSTRAP);
    GetSources(mirrors, folder, enabled);
    {
        LOCK(cs_bootstrap);
        info = BootstrapInfo();
        for (const std::string& url : mirrors)
            info.url += (info.url.empty() ? "" : " ") + url;
        if (!folder.empty())
            info.url = folder;
        info.configured = enabled && (!mirrors.empty() || !folder.empty());
        if (!info.configured)
            return;
    }
    LogPrintf("Bootstrap: block archives from %s\n", folder.empty() ? boost::algorithm::join(mirrors, ", ") : folder);
    Launch(folder.empty() ? mirrors : std::vector<std::string>(), folder);
}

bool StartBootstrapFromFolder(const std::string& folder, std::string& error)
{
    boost::system::error_code ec;
    if (folder.empty() || !fs::exists(fs::path(folder) / "manifest.json", ec)) {
        error = "there is no manifest.json in this folder";
        return false;
    }
    if (!Launch(std::vector<std::string>(), folder)) {
        error = "a bootstrap is running already";
        return false;
    }
    LogPrintf("Bootstrap: block archives from the folder %s\n", folder);
    return true;
}

void SetBootstrapFetcher(BootstrapFetcher function, bool https)
{
    fetcher = function ? function : BootstrapFetcher(HttpGet);
    fFetcherHttps = function ? https : false;
}

bool BootstrapCancelled()
{
    return Cancelled();
}

bool BootstrapHoldsBlockDownload()
{
    return fHold;
}

void SkipBootstrap()
{
    if (!fHold)
        return;
    LogPrintf("Bootstrap: given up by the user\n");
    fSkip = true;
    // the peers are asked for blocks at once; the thread tidies up when it notices
    fHold = false;
    LOCK(cs_bootstrap);
    info.active = false;
    info.state = "skipped";
}

BootstrapInfo GetBootstrapInfo()
{
    LOCK(cs_bootstrap);
    BootstrapInfo copy = info;
    if (copy.active && nStartTime > 0)
        copy.secondsTotal = GetTime() - nStartTime;
    if (pshared) {
        // the download goes on while an archive is imported
        copy.bytesDownloaded = pshared->bytes;
        copy.chunksDownloaded = pshared->downloaded;
        copy.secondsDownload = pshared->downloadMillis / 1000;
    }
    return copy;
}
