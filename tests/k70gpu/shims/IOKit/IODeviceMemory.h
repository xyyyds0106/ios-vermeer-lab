#include "XnuMemoryFixture.h"
#include <cassert>
enum {kIOMapAnywhere=1,kIOMapInhibitCache=2};
class IOMemoryMap {
public:
    size_t getLength(){assert(false);return 0;}
    uintptr_t getVirtualAddress(){assert(false);return 0;}
};
class IODeviceMemory {
public:
    static IODeviceMemory *withRange(uint64_t,size_t){assert(false);return nullptr;}
    IOMemoryMap *map(unsigned){assert(false);return nullptr;}
    void release(){assert(false);}
};
