#include "phantomx/blockchain/chain.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using phantomx::blockchain::ChainDatabase;

static ChainDatabase::VerifierChallengeRecord mkrec(const std::string& id, const std::string& vid, const std::string& owner, std::uint64_t now) {
  ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id=id; r.verifier_id=vid; r.pubkey_hex=std::string(64,'1'); r.owner=owner;
  r.network="regtest"; r.payload_blob={1,2,3}; r.issued_at=now; r.expires_at=now+300; r.status="issued"; return r;
}

int main(int argc,char**argv){
  // usage: <db> <mode:per|global> <initial> <nprocs> <now> <barrier> <outdir>
  if(argc<8){ std::cerr<<"usage\n"; return 2; }
  std::filesystem::path db=argv[1]; std::string mode=argv[2]; int initial=std::stoi(argv[3]); int nprocs=std::stoi(argv[4]);
  std::uint64_t now=std::stoull(argv[5]); std::filesystem::path barrier=argv[6]; std::filesystem::path outdir=argv[7];
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

  std::vector<pid_t> pids;
  for(int i=0;i<nprocs;++i){
    pid_t pid=fork();
    if(pid==0){
      auto dbh=ChainDatabase::open(db);
      auto res_path = outdir/("res_"+std::to_string(i)+".txt");
      auto tmp_path = outdir/("res_"+std::to_string(i)+".tmp");
      std::ofstream out(tmp_path, std::ios::trunc);
      if(!dbh.ok()){ out<<"err\n"<<dbh.error().to_string()<<"\n"; out.flush(); out.close(); std::filesystem::rename(tmp_path,res_path); _exit(3); }
      for(int w=0;w<30000;++w){ if(std::filesystem::exists(barrier)) break; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
      std::string cid(64,'f'); for(int k=0;k<8;++k) cid[k]=static_cast<char>('a'+((i>>(k*4))&0xF)); cid[8]='p';
      std::string vid = (mode=="per")?"v1":("nv"+std::to_string(i));
      std::string owner = (mode=="per")?"o1":("no"+std::to_string(i));
      auto pr=dbh.value().put_verifier_challenge_with_quota(mkrec(cid,vid,owner,now), now, 4096, 8, 3600, 256);
      if(pr.ok()) out<<"ok\n";
      else {
        const auto& m=pr.error().message();
        if(m=="challenge_capacity_global" || m=="challenge_capacity_identity") out<<"reject\n"<<m<<"\n";
        else out<<"err\n"<<pr.error().to_string()<<"\n";
      }
      out.flush(); out.close();
      std::error_code ec; std::filesystem::rename(tmp_path,res_path,ec);
      (void)dbh.value().close();
      _exit(pr.ok()?0:1);
    }
    pids.push_back(pid);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  { std::ofstream b(barrier, std::ios::trunc); b<<"go"; }
  int oks=0,rejects=0,errs=0;
  for(auto pid: pids){ int st=0; waitpid(pid,&st,0); }
  for(int i=0;i<nprocs;++i){
    std::ifstream in(outdir/("res_"+std::to_string(i)+".txt")); std::string line; std::getline(in,line);
    if(line=="ok") oks++; else if(line=="reject") rejects++; else errs++;
  }
  auto c2=ChainDatabase::open(db); if(!c2.ok()){ std::cerr<<c2.error().to_string(); return 5; }
  auto g=c2.value().count_active_verifier_challenges(now); if(!g.ok()){ std::cerr<<g.error().to_string(); return 6; }
  uint64_t idc=0; if(mode=="per"){ auto ic=c2.value().count_active_verifier_challenges_for("v1","o1",now); if(!ic.ok()) return 7; idc=ic.value(); }
  // integrity check
  auto icheck = c2.value().db().integrity_check();
  (void)c2.value().close();
  std::cout<<"ok="<<oks<<" reject="<<rejects<<" err="<<errs<<" global="<<g.value()<<" identity="<<idc<<" integrity="<<(icheck.ok()?"ok":"fail")<<"\n";
  if(errs!=0) return 10;
  if(!icheck.ok()) return 11;
  if(mode=="per"){ if(oks!=1 || rejects!=nprocs-1 || idc>8) return 12; }
  else { if(oks!=1 || rejects!=nprocs-1 || g.value()>4096) return 13; }
  return 0;
}
