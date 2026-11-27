#include "phantomx/blockchain/chain.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using phantomx::blockchain::ChainDatabase;

static ChainDatabase::VerifierChallengeRecord mkrec(const std::string& id, const std::string& vid, const std::string& owner, std::uint64_t now) {
  ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id=id; r.verifier_id=vid; r.pubkey_hex=std::string(64,'1'); r.owner=owner;
  r.network="regtest"; r.payload_blob={1,2,3}; r.issued_at=now; r.expires_at=now+300; r.status="issued"; return r;
}

int main(int argc,char**argv){
  // usage: <db> <mode per|global> <initial> <threads> <now> <barrier> <outdir> <events_csv>
  if(argc<9){ std::cerr<<"usage\n"; return 2; }
  std::filesystem::path db=argv[1]; std::string mode=argv[2]; int initial=std::stoi(argv[3]); int threads_n=std::stoi(argv[4]);
  std::uint64_t now=std::stoull(argv[5]); std::filesystem::path barrier=argv[6]; std::filesystem::path outdir=argv[7]; std::filesystem::path events_csv=argv[8];
  std::filesystem::create_directories(outdir);
  std::filesystem::remove(db); std::filesystem::remove(db.string()+"-wal"); std::filesystem::remove(db.string()+"-shm");

  auto c=ChainDatabase::open(db); if(!c.ok()){ std::cerr<<c.error().to_string(); return 3; }
  for(int i=0;i<initial;++i){
    std::string cid(64,'a'); for(int k=0;k<8;++k) cid[k]=static_cast<char>('a'+((i>>(k*4))&0xF));
    std::string vid = (mode=="per")?"v1":("v"+std::to_string(i));
    std::string owner = (mode=="per")?"o1":("o"+std::to_string(i));
    auto pr=c.value().put_verifier_challenge(mkrec(cid,vid,owner,now)); if(!pr.ok()){ std::cerr<<pr.error().to_string(); return 4; }
  }
  (void)c.value().close();

  std::mutex ev_mu;
  std::ofstream ev(events_csv, std::ios::trunc);
  ev << "kind,mono_seq,op_id,tx_id,conn_id,open_attempt_id,generation,role,pid,tid,retry,stage,operation_name,op_seq,api,ok,primary,extended,rc,elapsed_us,tx_active,rb_attempted,rb_ok,orig_primary,orig_extended,orig_rc,orig_api,rb_primary,rb_extended,rb_rc,rb_api,final_result\n";
  phantomx::db::SqliteDb::set_connection_observer([&](const phantomx::db::SqliteConnectionEvent& e){
    std::lock_guard<std::mutex> lk(ev_mu);
    ev << "conn,"<<e.monotonic_seq<<",0,0,"<<e.connection_id<<','<<e.open_attempt_id<<','<<e.lifecycle_generation<<','<<e.database_role<<','<<e.process_id<<','<<e.thread_id<<",0,"<<e.stage<<','<<e.operation_name<<','<<e.operation_seq<<','<<e.sqlite_api<<','<<(e.ok?1:0)<<','<<e.sqlite_primary<<','<<e.sqlite_extended<<','<<e.sqlite_rc<<','<<e.elapsed_us<<",0,0,0,0,0,0,,0,0,0,,\n";
  }, true);

  std::atomic<int> ok_count{0}, reject_count{0}, err_count{0};
  std::vector<std::thread> threads; threads.reserve(threads_n);
  for(int i=0;i<threads_n;++i){
    threads.emplace_back([&,i]{
      auto dbh=ChainDatabase::open(db);
      if(!dbh.ok()){ 
        err_count++; 
        std::ofstream out(outdir/("res_"+std::to_string(i)+".txt")); out<<"err\n"<<dbh.error().to_string()<<"\n"; return; 
      }
      dbh.value().set_quota_tx_observer([&](const ChainDatabase::QuotaTxDiagEvent& e){
        std::lock_guard<std::mutex> lk(ev_mu);
        ev << "tx,0,"<<e.operation_id<<','<<e.transaction_id<<','<<e.connection_id<<",0,0,quota,"<<e.process_id<<','<<e.thread_id<<','<<e.retry_attempt<<','<<e.stage<<",,0,"<<e.sqlite_api<<','<<(e.ok?1:0)<<','<<e.sqlite_primary<<','<<e.sqlite_extended<<','<<e.sqlite_rc<<','<<e.elapsed_us<<','<<(e.tx_active?1:0)<<','<<(e.rollback_attempted?1:0)<<','<<(e.rollback_ok?1:0)<<','<<e.original_primary<<','<<e.original_extended<<','<<e.original_rc<<','<<e.original_api<<','<<e.rollback_primary<<','<<e.rollback_extended<<','<<e.rollback_rc<<','<<e.rollback_api<<','<<e.final_result<<"\n";
      }, true);
      for(int w=0;w<30000;++w){ if(std::filesystem::exists(barrier)) break; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
      std::string cid(64,'f'); for(int k=0;k<8;++k) cid[k]=static_cast<char>('a'+((i>>(k*4))&0xF)); cid[8]='x';
      std::string vid = (mode=="per")?"v1":("nv"+std::to_string(i));
      std::string owner = (mode=="per")?"o1":("no"+std::to_string(i));
      auto pr=dbh.value().put_verifier_challenge_with_quota(mkrec(cid,vid,owner,now), now, 4096, 8, 3600, 256);
      dbh.value().clear_quota_tx_observer();
      std::ofstream out(outdir/("res_"+std::to_string(i)+".txt"), std::ios::trunc);
      if(pr.ok()){ ok_count++; out<<"ok\n"; }
      else {
        const auto& m=pr.error().message();
        if(m=="challenge_capacity_global"||m=="challenge_capacity_identity"){ reject_count++; out<<"reject\n"<<m<<"\n"; }
        else { err_count++; out<<"err\n"<<pr.error().to_string()<<"\n"; }
      }
      (void)dbh.value().close();
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  { std::ofstream b(barrier, std::ios::trunc); b<<"go"; }
  for(auto& t:threads) t.join();
  phantomx::db::SqliteDb::clear_connection_observer();
  ev.flush(); ev.close();

  auto c2=ChainDatabase::open(db); if(!c2.ok()){ std::cerr<<c2.error().to_string(); return 5; }
  auto g=c2.value().count_active_verifier_challenges(now); if(!g.ok()){ std::cerr<<g.error().to_string(); return 6; }
  uint64_t idc=0; if(mode=="per"){ auto ic=c2.value().count_active_verifier_challenges_for("v1","o1",now); if(!ic.ok()) return 7; idc=ic.value(); }
  auto icheck=c2.value().db().integrity_check(); (void)c2.value().close();
  std::cout<<"ok="<<ok_count.load()<<" reject="<<reject_count.load()<<" err="<<err_count.load()
           <<" global="<<g.value()<<" identity="<<idc<<" integrity="<<(icheck.ok()?"ok":"fail")<<"\n";
  if(err_count.load()!=0) return 10;
  if(!icheck.ok()) return 11;
  if(mode=="per"){ if(ok_count.load()!=1||reject_count.load()!=threads_n-1||idc>8) return 12; }
  else { if(ok_count.load()!=1||reject_count.load()!=threads_n-1||g.value()>4096) return 13; }
  return 0;
}
