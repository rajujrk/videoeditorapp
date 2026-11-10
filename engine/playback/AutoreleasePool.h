#pragma once

// The Objective-C runtime's autorelease pool, for C++ code on threads of its own. Core
// Video, VideoToolbox and Metal hand back autoreleased objects; on a long-lived thread
// they pile up until the thread ends unless a pool is drained per unit of work (a frame).
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);

namespace tve {

class AutoreleasePool {
public:
    AutoreleasePool() : m_pool(objc_autoreleasePoolPush()) {}
    ~AutoreleasePool() { objc_autoreleasePoolPop(m_pool); }
    AutoreleasePool(const AutoreleasePool&) = delete;
    AutoreleasePool& operator=(const AutoreleasePool&) = delete;

private:
    void* m_pool;
};

} // namespace tve
