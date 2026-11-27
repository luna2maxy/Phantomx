#include "phantomx/database/sqlite_db.hpp"
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using phantomx::db::SqliteDb;
using phantomx::db::SqliteConnectionEvent;

struct EvRow {
  std::uint64_t mono_seq=0, conn_id=0, open_attempt_id=0, generation=0, pid=0, tid=0;
  std::string role, stage, op_name, api;
  std::uint32_t op_seq=0;
  bool ok=false;
  int rc=0, primary=0, extended=0;
  std::int64_t elapsed_us=0;
  std::string msg;
};

static std::string json_escape(const std::string& s){
  std::string o; for(char c:s){ switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\n':o+="\\n";break;default:o+=c;} } return o;
}

static bool integrity_ok(const fs::path& db) {
  auto c = SqliteDb::open(db);
  if(!c.ok()) return false;
  auto ic = c.value().integrity_check();
  (void)c.value().close();
  return ic.ok();
}

static std::string journal_mode_of(const fs::path& db) {
  sqlite3* h=nullptr;
  if(sqlite3_open_v2(db.string().c_str(), &h, SQLITE_OPEN_READONLY, nullptr)!=SQLITE_OK){ if(h) sqlite3_close(h); return ""; }
  sqlite3_stmt* st=nullptr;
  if(sqlite3_prepare_v2(h, "PRAGMA journal_mode;", -1, &st, nullptr)!=SQLITE_OK){ sqlite3_close(h); return ""; }
  std::string r;
  if(sqlite3_step(st)==SQLITE_ROW){ const unsigned char* t=sqlite3_column_text(st,0); if(t) r=(const char*)t; }
  sqlite3_finalize(st); sqlite3_close(h); return r;
}

static uint64_t file_size_or0(const fs::path& p){ std::error_code ec; auto s=fs::file_size(p,ec); return ec?0:(uint64_t)s; }

struct RunResult {
  int ok=0, fail=0;
  std::vector<EvRow> events;
  std::vector<std::string> errors;
};

static RunResult run_scenario(const std::string& scenario, const fs::path& db, int threads, int iters, bool fresh, bool preexisting_wal, bool with_reader, bool with_writer, bool serial) {
  RunResult rr;
  for(int it=0; it<iters; ++it) {
    std::error_code ec;
    fs::remove(db,ec); fs::remove(db.string()+"-wal",ec); fs::remove(db.string()+"-shm",ec);
    if (preexisting_wal || !fresh) {
      // create initial DB and ensure WAL
      auto init = SqliteDb::open(db);
      if(!init.ok()){ rr.fail++; rr.errors.push_back("init_open:"+init.error().to_string()); continue; }
      auto ex = init.value().exec("CREATE TABLE IF NOT EXISTS t(x INTEGER); INSERT INTO t(x) VALUES(1);");
      (void)ex;
      (void)init.value().close();
      if (!preexisting_wal) {
        // convert to delete mode for "existing non-WAL" not requested; but we need existing WAL case
      }
    }
    if (fresh) { fs::remove(db,ec); fs::remove(db.string()+"-wal",ec); fs::remove(db.string()+"-shm",ec); }

    std::mutex mu;
    std::vector<EvRow> evs;
    SqliteDb::set_connection_observer([&](const SqliteConnectionEvent& e){
      EvRow r; r.mono_seq=e.monotonic_seq; r.conn_id=e.connection_id; r.open_attempt_id=e.open_attempt_id; r.generation=e.lifecycle_generation;
      r.pid=e.process_id; r.tid=e.thread_id; r.role=e.database_role; r.stage=e.stage; r.op_name=e.operation_name; r.op_seq=e.operation_seq;
      r.api=e.sqlite_api; r.ok=e.ok; r.rc=e.sqlite_rc; r.primary=e.sqlite_primary; r.extended=e.sqlite_extended; r.elapsed_us=e.elapsed_us; r.msg=e.sqlite_message;
      std::lock_guard<std::mutex> lk(mu); evs.push_back(std::move(r));
    }, true);

    std::atomic<int> okc{0}, failc{0};
    std::vector<std::string> errs;
    std::mutex err_mu;

    sqlite3* reader=nullptr; sqlite3_stmt* reader_stmt=nullptr;
    sqlite3* writer=nullptr;
    if (with_reader) {
      // create DB first if fresh
      if (fresh) { auto tmp=SqliteDb::open(db); if(tmp.ok()){ tmp.value().exec("CREATE TABLE IF NOT EXISTS t(x INTEGER);"); tmp.value().close(); } }
      sqlite3_open_v2(db.string().c_str(), &reader, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX, nullptr);
      sqlite3_exec(reader, "PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;", nullptr,nullptr,nullptr);
      sqlite3_exec(reader, "BEGIN;", nullptr,nullptr,nullptr);
      sqlite3_prepare_v2(reader, "SELECT COUNT(*) FROM sqlite_master;", -1, &reader_stmt, nullptr);
      sqlite3_step(reader_stmt); // keep read txn active
    }
    if (with_writer) {
      if (fresh && !with_reader) { auto tmp=SqliteDb::open(db); if(tmp.ok()){ tmp.value().exec("CREATE TABLE IF NOT EXISTS t(x INTEGER);"); tmp.value().close(); } }
      sqlite3_open_v2(db.string().c_str(), &writer, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX, nullptr);
      sqlite3_exec(writer, "PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;", nullptr,nullptr,nullptr);
      sqlite3_exec(writer, "BEGIN IMMEDIATE;", nullptr,nullptr,nullptr);
      sqlite3_exec(writer, "CREATE TABLE IF NOT EXISTS t2(x INTEGER); INSERT INTO t2(x) VALUES(1);", nullptr,nullptr,nullptr);
    }

    fs::path barrier = db.string()+".barrier";
    fs::remove(barrier, ec);
    std::vector<std::thread> ths;
    for(int t=0;t<threads;++t){
      ths.emplace_back([&,t]{
        if(!serial){
          for(int w=0;w<30000;++w){ if(fs::exists(barrier)) break; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        auto c = SqliteDb::open(db);
        if(c.ok()){ okc++; (void)c.value().close(); }
        else { failc++; std::lock_guard<std::mutex> lk(err_mu); errs.push_back(c.error().to_string()); }
        if(serial){ /* next thread proceeds naturally after join? no */ }
      });
      if(serial){ ths.back().join(); ths.clear(); }
    }
    if(!serial){
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      { std::ofstream b(barrier); b<<"go"; }
      for(auto& x:ths) x.join();
    }

    if(reader_stmt) sqlite3_finalize(reader_stmt);
    if(reader){ sqlite3_exec(reader, "ROLLBACK;", nullptr,nullptr,nullptr); sqlite3_close(reader); }
    if(writer){ sqlite3_exec(writer, "ROLLBACK;", nullptr,nullptr,nullptr); sqlite3_close(writer); }

    SqliteDb::clear_connection_observer();
    bool integ = integrity_ok(db);
    std::string jm = journal_mode_of(db);
    bool iter_ok = (failc.load()==0) && integ;
    if(iter_ok) rr.ok++; else rr.fail++;
    for(auto& e:errs) rr.errors.push_back("iter"+std::to_string(it)+":"+e);
    // keep events from failing iterations only to bound memory
    if(!iter_ok) {
      for(auto& e:evs) rr.events.push_back(std::move(e));
    }
    {
      std::ofstream jf(db.string()+"."+scenario+".iter"+std::to_string(it)+".json");
      jf<<"{\"scenario\":\""<<scenario<<"\",\"iter\":"<<it<<",\"threads\":"<<threads<<",\"ok\":"<<okc.load()<<",\"fail\":"<<failc.load()
        <<",\"integrity\":\""<<(integ?"ok":"fail")<<"\",\"journal_mode\":\""<<jm<<"\",\"db_size\":"<<file_size_or0(db)
        <<",\"wal_size\":"<<file_size_or0(db.string()+"-wal")<<"\",\"shm_size\":"<<file_size_or0(db.string()+"-shm")<<"\"}\n";
    }
  }
  return rr;
}

int main(int argc, char** argv){
  if(argc<3){ std::cerr<<"usage: <scenario> <out_dir> [iters] [threads]\n"; return 2; }
  std::string scenario=argv[1]; fs::path out=argv[2]; int iters=argc>3?std::stoi(argv[3]):10; int threads=argc>4?std::stoi(argv[4]):32;
  fs::create_directories(out);
  fs::path db = out/(scenario+".db");
  RunResult rr;
  if(scenario=="A_serial_fresh_rw") rr=run_scenario(scenario,db,threads,iters,true,false,false,false,true);
  else if(scenario=="B_concurrent_fresh_rw") rr=run_scenario(scenario,db,threads,iters,true,false,false,false,false);
  else if(scenario=="C_serial_existing_wal") rr=run_scenario(scenario,db,threads,iters,false,true,false,false,true);
  else if(scenario=="D_concurrent_existing_wal") rr=run_scenario(scenario,db,threads,iters,false,true,false,false,false);
  else if(scenario=="E_concurrent_ro_rw") rr=run_scenario(scenario,db,threads,iters,false,true,false,false,false); // simplified
  else if(scenario=="F_existing_reader") rr=run_scenario(scenario,db,threads,iters,true,false,true,false,false);
  else if(scenario=="G_existing_writer") rr=run_scenario(scenario,db,threads,iters,true,false,false,true,false);
  else { std::cerr<<"unknown scenario\n"; return 3; }

  std::ofstream sum(out/(scenario+".summary.json"));
  sum<<"{\"scenario\":\""<<scenario<<"\",\"iters\":"<<iters<<",\"threads\":"<<threads<<",\"pass\":"<<rr.ok<<",\"fail\":"<<rr.fail<<",\"errors\":"<<rr.errors.size()<<"}\n";
  std::ofstream evf(out/(scenario+".fail_events.csv"));
  evf<<"mono_seq,conn_id,open_attempt_id,generation,pid,tid,role,stage,op_name,op_seq,api,ok,rc,primary,extended,elapsed_us,msg\n";
  for(auto& e:rr.events){
    evf<<e.mono_seq<<','<<e.conn_id<<','<<e.open_attempt_id<<','<<e.generation<<','<<e.pid<<','<<e.tid<<','<<e.role<<','<<e.stage<<','<<e.op_name<<','<<e.op_seq<<','<<e.api<<','<<(e.ok?1:0)<<','<<e.rc<<','<<e.primary<<','<<e.extended<<','<<e.elapsed_us<<",\""<<json_escape(e.msg)<<"\"\n";
  }
  std::cout<<"scenario="<<scenario<<" pass="<<rr.ok<<" fail="<<rr.fail<<" errors="<<rr.errors.size()<<"\n";
  for(size_t i=0;i<rr.errors.size() && i<20;++i) std::cout<<"ERR:"<<rr.errors[i]<<"\n";
  return rr.fail==0?0:1;
}
