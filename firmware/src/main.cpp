#include <iostream>
using namespace std;
// TODO: Ignore this its just to populate the folder
// TODO: startup order once the pieces exist:
//   1. Database db;      db.open("");                        // CSV backup (Database.cpp TODO 2)
//   2. SupabaseSync sync;
//      if (sync.start(SyncConfig::from_env()))              // no env -> CSV only
//          sync.enqueue(db.rows_after(sync.last_synced_race()));   // upload anything missed
//   3. start sensor thread, then run_race(tree, db, sync)
//   4. on shutdown (SIGTERM from systemd): sync.stop(); db.close();
int main() {
  cout << "Hello World!";
  return 0;
}