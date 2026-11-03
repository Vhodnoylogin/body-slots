/* Independent C caller of the fixed binary table, without C++ header reuse.
 * This verifies C linkage/layout; it does not claim the C++ SDK header is C syntax.
 * Never invokes SKSEPlugin_Load or initializes the game. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t (__cdecl *RegisterClient)(const char*,uint64_t*);
typedef uint32_t (__cdecl *ReleaseClient)(uint64_t);
typedef uint32_t (__cdecl *RegisterObserver)(uint64_t*);
typedef struct BaseApi {
    uint32_t size,version; uint64_t capabilities;
    RegisterClient registerClient; ReleaseClient release;
    void (__cdecl *unused[5])(void);
    RegisterObserver observer;
} BaseApi;
typedef struct View {
    uint32_t size,version; uint64_t epoch,save,revision,sequence;
    uint32_t count,reserved;
} View;
typedef struct Frame {
    uint32_t size,reserved; uint64_t epoch,save,frame; double seconds;
} Frame;
typedef uint32_t (__cdecl *Copy)(uint64_t,uint64_t,void*,uint32_t,View*);
typedef struct RegionsApi {
    uint32_t size,version; uint64_t capabilities;
    uint32_t (__cdecl *upsert)(uint64_t,const void*,uint64_t,void*);
    uint32_t (__cdecl *remove)(uint64_t,uint64_t,uint64_t);
    Copy regions;
    uint32_t (__cdecl *publish)(uint64_t,const Frame*,const void*,uint32_t,View*);
    Copy observations;
} RegionsApi;
typedef const BaseApi* (__cdecl *BaseQuery)(uint32_t,uint32_t);
typedef const RegionsApi* (__cdecl *RegionsQuery)(uint32_t,uint32_t);
_Static_assert(sizeof(BaseApi)==80,"base binary layout");
_Static_assert(sizeof(RegionsApi)==56,"regions binary layout");
_Static_assert(sizeof(View)==48 && sizeof(Frame)==40,"copied binary records");
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    HMODULE dll; BaseQuery baseQuery; RegionsQuery query;
    const BaseApi* base; const RegionsApi* regions;
    uint64_t observer=0,writer=0;
    View view={sizeof(View),1,0,0,0,0,0,0};
    Frame frame={sizeof(Frame),0,1,1,1,1.0};
    CHECK(argc==2);
    dll=LoadLibraryA(argv[1]); CHECK(dll);
    baseQuery=(BaseQuery)GetProcAddress(dll,"BodySlots_QueryInterface");
    query=(RegionsQuery)GetProcAddress(dll,"BodySlots_QueryRegionsInterface");
    CHECK(baseQuery && query);
    CHECK(!query(0,56) && !query(2,56) && !query(1,55) && !query(1,57));
    base=baseQuery(1,80); regions=query(1,56);
    CHECK(base && regions && regions->size==56 && regions->version==1 && regions->capabilities==3);
    CHECK(base->observer(&observer)==0);
    CHECK(regions->regions(observer,0,NULL,0,&view)==0 && view.count==0 && !view.epoch && !view.save);
    CHECK(regions->observations(observer,0,NULL,0,&view)==0 && view.count==0);
    CHECK(regions->publish(observer,&frame,NULL,0,&view)==6); /* read-only lease */
    CHECK(base->registerClient("c-regions-probe",&writer)==0);
    CHECK(regions->publish(writer,&frame,NULL,0,&view)==8); /* disconnected, no invented scope */
    CHECK(base->release(writer)==0 && base->release(observer)==0);
    CHECK(regions->observations(observer,0,NULL,0,&view)==6);
    CHECK(FreeLibrary(dll));
    puts("PASS C binary discovery, negotiation, copied observations and disconnected refusal");
    return 0;
}
