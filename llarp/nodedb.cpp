#include "nodedb.hpp"

#include "router_contact.hpp"
#include "crypto/crypto.hpp"
#include "crypto/types.hpp"
#include "util/buffer.hpp"
#include "util/fs.hpp"
#include "util/logging.hpp"
#include "util/time.hpp"
#include "util/mem.hpp"
#include "util/str.hpp"
#include "dht/kademlia.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

static const char skiplist_subdirs[] = "0123456789abcdef";
static const std::string RC_FILE_EXT = ".signed";

namespace llarp
{
  static auto logcat = log::Cat("nodedb");

  NodeDB::Entry::Entry(RouterContact value) : rc(std::move(value)), insertedAt(llarp::time_now_ms())
  {}

  static void
  EnsureSkiplist(fs::path nodedbDir)
  {
    if (not fs::exists(nodedbDir))
    {
      // if the old 'netdb' directory exists, move it to this one
      fs::path parent = nodedbDir.parent_path();
      fs::path old = parent / "netdb";
      if (fs::exists(old))
        fs::rename(old, nodedbDir);
      else
        fs::create_directory(nodedbDir);
    }

    if (not fs::is_directory(nodedbDir))
      throw std::runtime_error{fmt::format("nodedb {} is not a directory", nodedbDir)};

    for (const char& ch : skiplist_subdirs)
    {
      // this seems to be a problem on all targets
      // perhaps cpp17::fs is just as screwed-up
      // attempting to create a folder with no name
      // what does this mean...?
      if (!ch)
        continue;

      fs::path sub = nodedbDir / std::string(&ch, 1);
      fs::create_directory(sub);
    }
  }

  constexpr auto FlushInterval = 5min;

  NodeDB::NodeDB(fs::path root, std::function<void(std::function<void()>)> diskCaller, Router* r)
      : router{*r}
      , m_Root{std::move(root)}
      , disk(std::move(diskCaller))
      , m_NextFlushAt{time_now_ms() + FlushInterval}
  {
    EnsureSkiplist(m_Root);
  }

  void
  NodeDB::Tick(llarp_time_t now)
  {
    if (m_NextFlushAt == 0s)
      return;

    if (now > m_NextFlushAt)
    {
      m_NextFlushAt += FlushInterval;
      // make copy of all rcs
      std::vector<RouterContact> copy;
      for (const auto& item : entries)
        copy.push_back(item.second.rc);
      // flush them to disk in one big job
      // TODO: split this up? idk maybe some day...
      disk([this, data = std::move(copy)]() {
        for (const auto& rc : data)
        {
          rc.Write(get_path_by_pubkey(rc.pubkey));
        }
      });
    }
  }

  fs::path
  NodeDB::get_path_by_pubkey(RouterID pubkey) const
  {
    std::string hexString = oxenc::to_hex(pubkey.begin(), pubkey.end());
    std::string skiplistDir;

    const llarp::RouterID r{pubkey};
    std::string fname = r.ToString();

    skiplistDir += hexString[0];
    fname += RC_FILE_EXT;
    return m_Root / skiplistDir / fname;
  }

  void
  NodeDB::load_from_disk()
  {
    if (m_Root.empty())
      return;
    std::set<fs::path> purge;

    for (const char& ch : skiplist_subdirs)
    {
      if (!ch)
        continue;
      std::string p;
      p += ch;
      fs::path sub = m_Root / p;

      llarp::util::IterDir(sub, [&](const fs::path& f) -> bool {
        // skip files that are not suffixed with .signed
        if (not(fs::is_regular_file(f) and f.extension() == RC_FILE_EXT))
          return true;

        RouterContact rc{};

        if (not rc.Read(f))
        {
          // try loading it, purge it if it is junk
          purge.emplace(f);
          return true;
        }

        if (not rc.FromOurNetwork())
        {
          // skip entries that are not from our network
          return true;
        }

        if (rc.IsExpired(time_now_ms()))
        {
          // rc expired dont load it and purge it later
          purge.emplace(f);
          return true;
        }

        // validate signature and purge entries with invalid signatures
        // load ones with valid signatures
        if (rc.VerifySignature())
          entries.emplace(rc.pubkey, rc);
        else
          purge.emplace(f);

        return true;
      });
    }

    if (not purge.empty())
    {
      log::warning(logcat, "removing {} invalid RCs from disk", purge.size());

      for (const auto& fpath : purge)
        fs::remove(fpath);
    }
  }

  void
  NodeDB::save_to_disk() const
  {
    if (m_Root.empty())
      return;

    router.loop()->call([this]() {
      for (const auto& item : entries)
        item.second.rc.Write(get_path_by_pubkey(item.first));
    });
  }

  bool
  NodeDB::has_router(RouterID pk) const
  {
    return router.loop()->call_get([this, pk]() { return entries.find(pk) != entries.end(); });
  }

  std::optional<RouterContact>
  NodeDB::get_rc(RouterID pk) const
  {
    return router.loop()->call_get([this, pk]() -> std::optional<RouterContact> {
      const auto itr = entries.find(pk);
      if (itr == entries.end())
        return std::nullopt;
      return itr->second.rc;
    });
  }

  void
  NodeDB::remove_router(RouterID pk)
  {
    router.loop()->call([this, pk]() {
      entries.erase(pk);
      remove_many_from_disk_async({pk});
    });
  }

  void
  NodeDB::remove_stale_rcs(std::unordered_set<RouterID> keep, llarp_time_t cutoff)
  {
    router.loop()->call([this, keep, cutoff]() {
      std::unordered_set<RouterID> removed;
      auto itr = entries.begin();
      while (itr != entries.end())
      {
        if (itr->second.insertedAt < cutoff and keep.count(itr->second.rc.pubkey) == 0)
        {
          removed.insert(itr->second.rc.pubkey);
          itr = entries.erase(itr);
        }
        else
          ++itr;
      }
      if (not removed.empty())
        remove_many_from_disk_async(std::move(removed));
    });
  }

  void
  NodeDB::put_rc(RouterContact rc)
  {
    router.loop()->call([this, rc]() {
      entries.erase(rc.pubkey);
      entries.emplace(rc.pubkey, rc);
    });
  }

  size_t
  NodeDB::num_loaded() const
  {
    return router.loop()->call_get([this]() { return entries.size(); });
  }

  void
  NodeDB::put_rc_if_newer(RouterContact rc)
  {
    router.loop()->call([this, rc]() {
      auto itr = entries.find(rc.pubkey);
      if (itr == entries.end() or itr->second.rc.OtherIsNewer(rc))
      {
        // delete if existing
        if (itr != entries.end())
          entries.erase(itr);
        // add new entry
        entries.emplace(rc.pubkey, rc);
      }
    });
  }

  void
  NodeDB::remove_many_from_disk_async(std::unordered_set<RouterID> remove) const
  {
    if (m_Root.empty())
      return;
    // build file list
    std::set<fs::path> files;
    for (auto id : remove)
    {
      files.emplace(get_path_by_pubkey(std::move(id)));
    }
    // remove them from the disk via the diskio thread
    disk([files]() {
      for (auto fpath : files)
        fs::remove(fpath);
    });
  }

  llarp::RouterContact
  NodeDB::find_closest_to(llarp::dht::Key_t location) const
  {
    return router.loop()->call_get([this, location]() {
      llarp::RouterContact rc;
      const llarp::dht::XorMetric compare(location);
      VisitAll([&rc, compare](const auto& otherRC) {
        if (rc.pubkey.IsZero())
        {
          rc = otherRC;
          return;
        }
        if (compare(
                llarp::dht::Key_t{otherRC.pubkey.as_array()},
                llarp::dht::Key_t{rc.pubkey.as_array()}))
          rc = otherRC;
      });
      return rc;
    });
  }

  std::vector<RouterContact>
  NodeDB::find_many_closest_to(llarp::dht::Key_t location, uint32_t numRouters) const
  {
    return router.loop()->call_get([this, location, numRouters]() {
      std::vector<const RouterContact*> all;

      all.reserve(entries.size());
      for (auto& entry : entries)
      {
        all.push_back(&entry.second.rc);
      }

      auto it_mid = numRouters < all.size() ? all.begin() + numRouters : all.end();
      std::partial_sort(
          all.begin(), it_mid, all.end(), [compare = dht::XorMetric{location}](auto* a, auto* b) {
            return compare(*a, *b);
          });

      std::vector<RouterContact> closest;
      closest.reserve(numRouters);
      for (auto it = all.begin(); it != it_mid; ++it)
        closest.push_back(**it);

      return closest;
    });
  }
}  // namespace llarp
