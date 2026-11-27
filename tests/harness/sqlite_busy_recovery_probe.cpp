#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static void pr(const char* op, int rc, sqlite3* db){
  std::cout<<op<<" rc="<<rc<<" primary="<<sqlite3_errcode(db)<<" ext="<<sqlite3_extended_errcode(db)<<" msg="<<(sqlite3_errmsg(db)?sqlite3_errmsg(db):"")<<"\n";
}

int main(int argc,char**argv){
  std::filesystem::path dbp = argc>1?argv[1]:"/tmp/phx_busy_recovery.db";
  std::filesystem::remove(dbp); std::filesystem::remove(dbp.string()+"-wal"); std::filesystem::remove(dbp.string()+"-shm");
  sqlite3 *a=nullptr,*b=nullptr;
  int rc=sqlite3_open_v2(dbp.string().c_str(), &a, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr);
  rc=sqlite3_open_v2(dbp.string().c_str(), &b, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr);
  sqlite3_busy_timeout(a, 50); sqlite3_busy_timeout(b, 50);
  char* err=nullptr;
  sqlite3_exec(a,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;",nullptr,nullptr,&err); if(err){sqlite3_free(err);err=nullptr;}
  sqlite3_exec(a,"CREATE TABLE t(id TEXT PRIMARY KEY, v INT);",nullptr,nullptr,&err); if(err){sqlite3_free(err);err=nullptr;}
  // long read on a
  sqlite3_exec(a,"BEGIN;",nullptr,nullptr,&err); if(err){sqlite3_free(err);err=nullptr;}
  sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(a,"SELECT COUNT(*) FROM t;",-1,&s,nullptr); sqlite3_step(s); sqlite3_finalize(s);
  // writer on b
  rc=sqlite3_exec(b,"INSERT INTO t(id,v) VALUES('x',1);",nullptr,nullptr,&err); pr("insert_b_during_read",rc,b); if(err){sqlite3_free(err);err=nullptr;}
  // checkpoint while read active
  rc=sqlite3_wal_checkpoint_v2(b,"main",SQLITE_CHECKPOINT_PASSIVE,nullptr,nullptr); pr("checkpoint_passive_b",rc,b);
  rc=sqlite3_exec(a,"COMMIT;",nullptr,nullptr,&err); if(err){sqlite3_free(err);err=nullptr;}
  rc=sqlite3_wal_checkpoint_v2(b,"main",SQLITE_CHECKPOINT_PASSIVE,nullptr,nullptr); pr("checkpoint_after_read_commit",rc,b);
  sqlite3_close(a); sqlite3_close(b);
  return 0;
}
