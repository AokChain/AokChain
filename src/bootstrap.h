// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_BOOTSTRAP_H
#define AOKCHAIN_BOOTSTRAP_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace boost { class thread_group; }

/**
 * Block bootstrap: a node that is far behind fetches the blocks as archives
 * over HTTP instead of block by block from its peers, and imports them.
 *
 * The archives are files in the bootstrap.dat format (see
 * contrib/aokchain/make_bootstrap.py) that are listed in a manifest. Every
 * block of an archive goes through ProcessNewBlock(), that is through the
 * same validation as a block from a peer. The host of the archives is
 * therefore not trusted: a wrong archive can only fail, and then the node
 * synchronizes from its peers as it always did.
 *
 * The sources are -bootstrapurl (one or several mirrors), -bootstrapdir (a
 * folder of this computer with the same files) or, on the main network, the
 * built-in address. -bootstrap=0 turns it off.
 */

static const bool DEFAULT_BOOTSTRAP = true;
/** Blocks that the node must be behind the archives for the bootstrap to be worth it */
static const int DEFAULT_BOOTSTRAP_MIN_BLOCKS = 20000;
/** No server is asked when the tip of the node is younger than this (seconds) */
static const int64_t BOOTSTRAP_MIN_TIP_AGE = 3 * 24 * 60 * 60;
/**
 * Where the block archives of the main network are published: used when neither -bootstrapurl nor
 * -bootstrapdir is given. Release assets, https with a redirect, so only a fetcher that does https
 * (the GUI) uses it. The other networks have no built-in address.
 */
static const char* const DEFAULT_BOOTSTRAP_URL_MAIN = "https://github.com/AokChain/Mainnet-blocks/releases/download/mainnet/";

struct BootstrapInfo
{
    bool configured;            //!< -bootstrapurl is set and -bootstrap is not 0
    std::string url;
    /** "off", "starting", "manifest", "running", "done", "notneeded", "failed" or "skipped" */
    std::string state;
    bool active;                //!< block download from peers is held back
    int chunks;                 //!< archives that this node needs
    int chunksDownloaded;
    int chunksImported;
    uint64_t bytesTotal;        //!< to download, for the archives that are needed
    uint64_t bytesDownloaded;
    int startHeight;            //!< height of the node when the bootstrap began
    int targetHeight;           //!< last height of the archives
    int64_t secondsDownload;    //!< time spent downloading so far
    int64_t secondsImport;      //!< time spent decompressing and validating so far
    int64_t secondsTotal;
    std::string message;        //!< why it failed, or what it is doing

    BootstrapInfo() : configured(false), state("off"), active(false), chunks(0), chunksDownloaded(0), chunksImported(0),
        bytesTotal(0), bytesDownloaded(0), startHeight(0), targetHeight(0), secondsDownload(0), secondsImport(0), secondsTotal(0) {}
};

/**
 * Fetches a file. With a path, the body is appended to that file after the bytes that are there
 * (resume with a Range request; when the server sends the whole file instead, the file is started
 * again); without a path the body is returned in body. progress is advanced by the bytes of the
 * file (those on disk and those that arrive). Must give up when BootstrapCancelled() turns true.
 */
typedef std::function<bool(const std::string& url, const std::string& path, uint64_t maxBytes,
                           std::atomic<uint64_t>* progress, std::string* body, std::string& error)> BootstrapFetcher;
/**
 * Replace the fetcher. The daemon fetches http:// with libevent; the GUI installs one that uses
 * the network stack of Qt and also does https:// and its redirects. Call before StartBootstrap().
 */
void SetBootstrapFetcher(BootstrapFetcher function, bool https);
/** For a fetcher: the download is to be given up (shutdown, or the user skipped the bootstrap). */
bool BootstrapCancelled();

/** Start the bootstrap thread when -bootstrapurl asks for it. Call once, when the block index is loaded. */
void StartBootstrap(boost::thread_group& threadGroup);
/**
 * Import the archives of a folder of this computer (manifest.json and the files it lists), which
 * were fetched by other means. The folder is only read. False when a bootstrap is running already
 * or the folder has no manifest.
 */
bool StartBootstrapFromFolder(const std::string& folder, std::string& error);
/** True while the bootstrap runs: the blocks are not requested from peers in that time. */
bool BootstrapHoldsBlockDownload();
/** Give the bootstrap up and synchronize from the peers. */
void SkipBootstrap();
BootstrapInfo GetBootstrapInfo();

#endif // AOKCHAIN_BOOTSTRAP_H
