#include "phantomx/database/sqlite_db.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
namespace fs=std::filesystem;
using phantomx::db::SqliteDb;

static uint64_t now_ns(){ return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct R{ int ok=0,fail=0,busy55=0,busy261=0,other=0; };
static void classify(R& r,int rc,int primary,int extended,bool ok){
  if(ok){r.ok++; return;}
  r.fail++;
  if(primary==5 && extended==261) r.busy261++;
  else if(primary==5 && extended==5) r.busy55++;
  else r.other++;
}

static R run_raw(const fs::path& db, int threads, bool serial){
  R r; std::mutex mu; std::atomic<bool> go{false}; std::atomic<int> arrived{0};
  auto worker=[&](){
    if(!serial){ arrived++; while(!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    sqlite3* h=nullptr;
    int rc=sqlite3_open_v2(db.string().c_str(), &h, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX, nullptr);
    if(rc!=SQLITE_OK){ std::lock_guard<std::mutex> lk(mu); classify(r,rc,sqlite3_extended_errcode(h)&0xFF,sqlite3_extended_errcode(h),false); if(h) sqlite3_close(h); return; }
    const char* pragmas[]={"PRAGMA journal_mode=WAL;","PRAGMA synchronous=FULL;","PRAGMA foreign_keys=ON;","PRAGMA temp_store=MEMORY;","PRAGMA busy_timeout=5000;",nullptr};
    bool ok=true; int pri=0,ext=0,erc=0;
    for(int i=0; pragmas[i]; ++i){ char* err=nullptr; erc=sqlite3_exec(h,pragmas[i],nullptr,nullptr,&err); if(err) sqlite3_free(err); if(erc!=SQLITE_OK){ pri=sqlite3_errcode(h); ext=sqlite3_extended_errcode(h); ok=false; break; } }
    sqlite3_close(h);
    std::lock_guard<std::mutex> lk(mu); classify(r,erc,pri,ext,ok);
  };
  std::vector<std::thread> ths;
  for(int i=0;i<threads;++i){ ths.emplace_back(worker); if(serial){ths.back().join(); ths.clear();} }
  if(!serial){ while(arrived.load()<threads) std::this_thread::sleep_for(std::chrono::milliseconds(1)); go.store(true); for(auto& t:ths) t.join(); }
  return r;
}

static R run_prod(const fs::path& db, int threads, bool serial){
  R r; std::mutex mu; std::atomic<bool> go{false}; std::atomic<int> arrived{0};
  auto worker=[&](){
    if(!serial){ arrived++; while(!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    auto c=SqliteDb::open(db);
    if(c.ok()){ (void)c.value().close(); std::lock_guard<std::mutex> lk(mu); classify(r,0,0,0,true); }
    else {
      auto msg=c.error().to_string(); int p=0,e=0,rc=0;
      auto pp=msg.find("primary="); auto ep=msg.find("extended="); auto rp=msg.find("rc=");
      if(pp!=std::string::npos) p=std::atoi(msg.c_str()+pp+8);
      if(ep!=std::string::npos) e=std::atoi(msg.c_str()+ep+9);
      if(rp!=std::string::npos) rc=std::atoi(msg.c_str()+rp+3);
      std::lock_guard<std::mutex> lk(mu); classify(r,rc,p,e,false);
    }
  };
  std::vector<std::thread> ths;
  for(int i=0;i<threads;++i){ ths.emplace_back(worker); if(serial){ths.back().join(); ths.clear();} }
  if(!serial){ while(arrived.load()<threads) std::this_thread::sleep_for(std::chrono::milliseconds(1)); go.store(true); for(auto& t:ths) t.join(); }
  return r;
}

static void print(const char* name,const R& r){ std::cout<<name<<" ok="<<r.ok<<" fail="<<r.fail<<" busy55="<<r.busy55<<" busy261="<<r.busy261<<" other="<<r.other<<"\n"; }

int main(){
  fs::path dir="/tmp/r19_rawprod"; fs::create_directories(dir);
  int threads=16;
  for(int iter=0; iter<3; ++iter){
    {
      auto db=dir/("fresh_raw_"+std::to_string(iter)+".db"); fs::remove(db); fs::remove(db.string()+"-wal"); fs::remove(db.string()+"-shm");
      auto r=run_raw(db,threads,false); print("fresh_raw_concurrent",r);
    }
    {
      auto db=dir/("fresh_prod_"+std::to_string(iter)+".db"); fs::remove(db); fs::remove(db.string()+"-wal"); fs::remove(db.string()+"-shm");
      auto r=run_prod(db,threads,false); print("fresh_prod_concurrent",r);
    }
  }
  std::cout<<"NOTE: limited comparison, same host/sqlite/flags/PRAGMA sequence; not a full equivalence proof\n";
  return 0;
}
