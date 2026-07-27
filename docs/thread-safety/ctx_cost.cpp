// Per-thread PJ_CONTEXT cost: each context lazily builds its own
// projCppContext -> DatabaseContext -> prepared statements + 10 LRU caches.
#include <proj.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <fstream>
static long rssKb(){ std::ifstream f("/proc/self/status"); std::string k; long v=0;
  while(f>>k){ if(k=="VmRSS:"){ f>>v; return v;} } return 0; }
static std::atomic<int> ready{0}; static std::atomic<bool> go{false};
int main(int argc,char**argv){
  const int n = argc>1?atoi(argv[1]):1;
  printf("baseline RSS %ld kB\n", rssKb());
  std::vector<std::thread> t;
  for(int i=0;i<n;i++) t.emplace_back([i]{
      PJ_CONTEXT* c = proj_context_create();
      static const int codes[]={4326,3067,3857,32635,2393,4258,3035,25835};
      for(int k=0;k<8;k++){ char b[32]; snprintf(b,sizeof b,"EPSG:%d",codes[k]);
        PJ* p=proj_create(c,b); if(p) proj_destroy(p); }
      ready++; while(!go) std::this_thread::yield();
      proj_context_destroy(c);
  });
  while(ready<n) std::this_thread::yield();
  printf("threads=%2d  RSS %ld kB\n", n, rssKb());
  go=true; for(auto&x:t) x.join(); return 0;
}
