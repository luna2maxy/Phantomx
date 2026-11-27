#include "phantomx/database/sqlite_db.hpp"
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <set>

using phantomx::db::SqliteDb;
using phantomx::db::SqliteConnectionEvent;
namespace fs=std::filesystem;

static int g_fail=0;
#define CHECK(cond,msg) do{ if(!(cond)){ std::cerr<<"FAIL: "<<msg<<"\n"; ++g_fail; } else { std::cout<<"PASS: "<<msg<<"\n"; } }while(0)

static bool wait_until(const std::function<bool()>& fn, int ms){
  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
  while(std::chrono::steady_clock::now()<end){ if(fn()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  return fn();
}

int main(){
  fs::path dir="/tmp/r19_obs";
  fs::create_directories(dir);

  // A. Success event capture
  {
    std::vector<SqliteConnectionEvent> evs; std::mutex mu;
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){ std::lock_guard<std::mutex> lk(mu); evs.push_back(e); }, true);
    auto dbp = dir/"a.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    auto c = SqliteDb::open(dbp);
    CHECK(c.ok(), "A open ok");
    c.value().close();
    SqliteDb::clear_connection_observer();
    bool has_ready=false, has_init=false;
    for(auto& e:evs){ if(e.stage=="CONNECTION_READY") has_ready=true; if(e.stage=="INITIALIZATION_BEGIN") has_init=true; }
    CHECK(has_init && has_ready, "A success events captured");
  }

  // B. Failure event capture - open readonly on missing file
  {
    std::vector<SqliteConnectionEvent> evs; std::mutex mu;
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){ std::lock_guard<std::mutex> lk(mu); evs.push_back(e); }, true);
    auto dbp = dir/"missing.db"; fs::remove(dbp);
    auto c = SqliteDb::open_readonly(dbp);
    CHECK(!c.ok(), "B open_readonly missing fails");
    SqliteDb::clear_connection_observer();
    bool has_fail=false;
    for(auto& e:evs){ if(e.stage=="OPEN_FAILURE" && !e.ok) has_fail=true; }
    CHECK(has_fail, "B failure event captured");
  }

  // C. Original error preservation - check error message has primary/extended
  {
    auto dbp = dir/"missing2.db"; fs::remove(dbp);
    auto c = SqliteDb::open_readonly(dbp);
    CHECK(!c.ok(), "C fails");
    if(!c.ok()){
      auto msg=c.error().to_string();
      CHECK(msg.find("primary=")!=std::string::npos, "C original error has primary");
      CHECK(msg.find("extended=")!=std::string::npos, "C original error has extended");
    }
  }

  // D. Disabled observer semantic equivalence
  {
    auto dbp = dir/"d.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    SqliteDb::clear_connection_observer();
    auto c1 = SqliteDb::open(dbp);
    CHECK(c1.ok(), "D open without observer ok");
    auto ex1 = c1.value().exec("CREATE TABLE t(x INTEGER); INSERT INTO t(x) VALUES(1);");
    CHECK(ex1.ok(), "D exec without observer ok");
    c1.value().close();
    std::vector<SqliteConnectionEvent> evs; std::mutex mu;
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){ std::lock_guard<std::mutex> lk(mu); evs.push_back(e); }, true);
    auto c2 = SqliteDb::open(dbp);
    CHECK(c2.ok(), "D open with observer ok");
    auto ex2 = c2.value().exec("INSERT INTO t(x) VALUES(2);");
    CHECK(ex2.ok(), "D exec with observer ok");
    c2.value().close();
    SqliteDb::clear_connection_observer();
    CHECK(!evs.empty(), "D observer received events");
  }

  // E. Concurrent callback invocation
  {
    std::atomic<int> count{0};
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent&){ count++; }, true);
    std::vector<std::thread> ths;
    for(int i=0;i<8;++i){
      ths.emplace_back([&,i]{
        auto dbp = dir/("e"+std::to_string(i)+".db"); fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
        auto c=SqliteDb::open(dbp);
        if(c.ok()) c.value().close();
      });
    }
    for(auto& t:ths) t.join();
    SqliteDb::clear_connection_observer();
    CHECK(count.load() > 0, "E concurrent callbacks invoked");
  }

  // F. Callback lifetime during close - no crash; observer cleared after close
  {
    std::atomic<int> count{0};
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent&){ count++; }, true);
    auto dbp = dir/"f.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    {
      auto c = SqliteDb::open(dbp);
      CHECK(c.ok(), "F open ok");
      c.value().close();
    }
    SqliteDb::clear_connection_observer();
    CHECK(count.load()>0, "F events during lifecycle");
  }

  // G. Reentrancy - callback must not deadlock if it does simple work
  {
    std::atomic<int> count{0};
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){ count++; (void)e.stage.size(); }, true);
    auto dbp = dir/"g.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    auto c=SqliteDb::open(dbp);
    CHECK(c.ok(), "G open ok");
    c.value().close();
    SqliteDb::clear_connection_observer();
    CHECK(count.load()>0, "G reentrancy-safe simple callback");
  }

  // H. Exception policy - callback throws should not crash harness; observer catches? implementation catches exceptions
  {
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent&){ throw std::runtime_error("boom"); }, true);
    auto dbp = dir/"h.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    auto c=SqliteDb::open(dbp);
    CHECK(c.ok(), "H open ok despite throwing observer");
    if(c.ok()) c.value().close();
    SqliteDb::clear_connection_observer();
  }

  // I. Sequence uniqueness and correlation completeness
  {
    std::vector<SqliteConnectionEvent> evs; std::mutex mu;
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){ std::lock_guard<std::mutex> lk(mu); evs.push_back(e); }, true);
    auto dbp = dir/"i.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    auto c=SqliteDb::open(dbp);
    CHECK(c.ok(), "I open ok");
    c.value().close();
    SqliteDb::clear_connection_observer();
    std::set<uint64_t> seqs;
    bool missing=false;
    for(auto& e:evs){ if(e.monotonic_seq==0) missing=true; seqs.insert(e.monotonic_seq); if(e.connection_id==0) missing=true; }
    CHECK(!missing, "I correlation fields present");
    CHECK(seqs.size()==evs.size(), "I monotonic seq unique in capture");
  }

  // J. No unintended serialization/deadlock - bounded timeout
  {
    std::atomic<int> count{0};
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent&){ count++; std::this_thread::sleep_for(std::chrono::microseconds(50)); }, true);
    auto t0=std::chrono::steady_clock::now();
    auto dbp = dir/"j.db"; fs::remove(dbp); fs::remove(dbp.string()+"-wal"); fs::remove(dbp.string()+"-shm");
    auto c=SqliteDb::open(dbp);
    bool ok=c.ok();
    if(ok) c.value().close();
    SqliteDb::clear_connection_observer();
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();
    CHECK(ok, "J open ok");
    CHECK(ms < 5000, "J bounded completion no deadlock");
  }

  std::cout << "observer_integrity failures="<<g_fail<<"\n";
  return g_fail==0?0:1;
}
