#ifndef ART_ROWEX_N_H
#define ART_ROWEX_N_H
//#define ART_NOREADLOCK
//#define ART_NOWRITELOCK
#include "Epoche.h"
#include "Key.h"
#include <atomic>
#include <stdint.h>
#include <string.h>
#ifdef LOCK_INIT
#include "tbb/concurrent_vector.h"
#endif

using TID = uint64_t;

namespace PART_ns {
/*
 * SynchronizedTree
 * LockCouplingTree
 * LockCheckFreeReadTree
 * UnsynchronizedTree
 */

enum class NTypes : uint8_t { N4 = 1, N16 = 2, N48 = 3, N256 = 4, Leaf = 5 };

class Leaf {
  public:
    size_t key_len;
    uint64_t key;
    //    uint8_t *fkey;
    // TODO: variable key
    uint8_t fkey[16];
    uint64_t value;

  public:
    Leaf(const Key *k) {
        key_len = k->key_len;
        value = k->value;
#ifdef KEY_INLINE
        key = k->key; // compare to store the key, new an array will decrease
                      // 30% performance
        fkey = (uint8_t *)&key;
#else
        //        fkey = new uint8_t[key_len];
        memcpy(fkey, k->fkey, key_len);
#endif
    }

    virtual ~Leaf() {
        // TODO
    }

    inline uint64_t getValue() { return value; }
    inline bool checkKey(const Key *k) const {
        if (key_len == k->getKeyLen() && memcmp(fkey, k->fkey, key_len) == 0)
            return true;
        return false;
    }
    inline size_t getKeyLen() const { return key_len; }
} __attribute__((aligned(64)));

static constexpr uint32_t maxStoredPrefixLength = 4;
struct Prefix {
    uint32_t prefixCount = 0;
    uint8_t prefix[maxStoredPrefixLength];
};
static_assert(sizeof(Prefix) == 8, "Prefix should be 64 bit long");
#ifdef LOCK_INIT
class N;
static tbb::concurrent_vector<N *> lock_initializer;
void lock_initialization();
#endif
class N {
  protected:
    N(NTypes type, uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : level(level) {
        setType(type);
        setPrefix(prefix, prefixLength, false);
#ifdef LOCK_INIT
        lock_initializer.push_back(this);
#endif
    }

    N(NTypes type, uint32_t level, const Prefix &prefi)
        : prefix(prefi), level(level) {
        setType(type);
#ifdef LOCK_INIT
        lock_initializer.push_back(this);
#endif
    }

    N(const N &) = delete;

    N(N &&) = delete;

    // 3b type 60b version 1b lock 1b obsolete
    std::atomic<uint64_t> typeVersionLockObsolete{0b100};
    // version 1, unlocked, not obsolete
    std::atomic<Prefix> prefix;
    const uint32_t level;
    uint16_t count = 0;
    uint16_t compactCount = 0;

    static const uint64_t dirty_bit = ((uint64_t)1 << 60);

    void setType(NTypes type);

    static uint64_t convertTypeToVersion(NTypes type);

  public:
    static inline N *setDirty(N *val) {
        return (N *)((uint64_t)val | dirty_bit);
    }
    static inline N *clearDirty(N *val) {
        return (N *)((uint64_t)val & (~dirty_bit));
    }
    static inline bool isDirty(N *val) { return (uint64_t)val & dirty_bit; }

    static inline void helpFlush(std::atomic<N *> *n);

    NTypes getType() const;

    uint32_t getLevel() const;

    uint32_t getCount() const;

    void setCount(uint16_t count_, uint16_t compactCount_);

    bool isLocked(uint64_t version) const;

    void writeLockOrRestart(bool &needRestart);

    void lockVersionOrRestart(uint64_t &version, bool &needRestart);

    void writeUnlock();

    uint64_t getVersion() const;

    /**
     * returns true if node hasn't been changed in between
     */
    bool checkOrRestart(uint64_t startRead) const;
    bool readUnlockOrRestart(uint64_t startRead) const;

    static bool isObsolete(uint64_t version);

    /**
     * can only be called when node is locked
     */
    void writeUnlockObsolete() { typeVersionLockObsolete.fetch_add(0b11); }

    static std::atomic<N *> *getChild(const uint8_t k, N *node);

    static void insertAndUnlock(N *node, N *parentNode, uint8_t keyParent,
                                uint8_t key, N *val, ThreadInfo &threadInfo,
                                bool &needRestart);

    static void change(N *node, uint8_t key, N *val);

    static void removeAndUnlock(N *node, uint8_t key, N *parentNode,
                                uint8_t keyParent, ThreadInfo &threadInfo,
                                bool &needRestart);

    Prefix getPrefi() const;

    inline void setPrefix(const uint8_t *prefix, uint32_t length, bool flush);

    void addPrefixBefore(N *node, uint8_t key);

    static Leaf *getLeaf(const N *n);

    static bool isLeaf(const N *n);

    static N *setLeaf(const Leaf *k);

    static N *getAnyChild(const N *n);

    static Leaf *getAnyChildTid(const N *n);

    static void deleteChildren(N *node);

    static void deleteNode(N *node);

    static std::tuple<N *, uint8_t> getSecondChild(N *node, const uint8_t k);

    template <typename curN, typename biggerN>
    static void insertGrow(curN *n, N *parentNode, uint8_t keyParent,
                           uint8_t key, N *val, NTypes type,
                           ThreadInfo &threadInfo, bool &needRestart);

    template <typename curN>
    static void insertCompact(curN *n, N *parentNode, uint8_t keyParent,
                              uint8_t key, N *val, NTypes type,
                              ThreadInfo &threadInfo, bool &needRestart);

    template <typename curN, typename smallerN>
    static void removeAndShrink(curN *n, N *parentNode, uint8_t keyParent,
                                uint8_t key, NTypes type,
                                ThreadInfo &threadInfo, bool &needRestart);

    static void getChildren(N *node, uint8_t start, uint8_t end,
                            std::tuple<uint8_t, std::atomic<N *> *> children[],
                            uint32_t &childrenCount);

    static void rebuild_node(N *node);

    static inline void mfence();

    static inline void clflush(char *data, int len, bool front, bool back);
};

class N4 : public N {
  public:
    std::atomic<uint8_t> keys[4];
    std::atomic<N *> children[4];

  public:
    N4(uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : N(NTypes::N4, level, prefix, prefixLength) {
        memset(keys, 0, sizeof(keys));
        memset(children, 0, sizeof(children));
    }

    N4(uint32_t level, const Prefix &prefi) : N(NTypes::N4, level, prefi) {
        memset(keys, 0, sizeof(keys));
        memset(children, 0, sizeof(children));
    }

    inline bool insert(uint8_t key, N *n, bool flush);

    template <class NODE> void copyTo(NODE *n) const;

    void change(uint8_t key, N *val);

    std::atomic<N *> *getChild(const uint8_t k);

    bool remove(uint8_t k, bool force, bool flush);

    N *getAnyChild() const;

    std::tuple<N *, uint8_t> getSecondChild(const uint8_t key) const;

    void deleteChildren();

    void getChildren(uint8_t start, uint8_t end,
                     std::tuple<uint8_t, std::atomic<N *> *> children[],
                     uint32_t &childrenCount) ;

    uint32_t getCount() const;
};

class N16 : public N {
  public:
    std::atomic<uint8_t> keys[16];
    std::atomic<N *> children[16];

    static uint8_t flipSign(uint8_t keyByte) {
        // Flip the sign bit, enables signed SSE comparison of unsigned values,
        // used by Node16
        return keyByte ^ 128;
    }

    static inline unsigned ctz(uint16_t x) {
        // Count trailing zeros, only defined for x>0
#ifdef __GNUC__
        return __builtin_ctz(x);
#else
        // Adapted from Hacker's Delight
        unsigned n = 1;
        if ((x & 0xFF) == 0) {
            n += 8;
            x = x >> 8;
        }
        if ((x & 0x0F) == 0) {
            n += 4;
            x = x >> 4;
        }
        if ((x & 0x03) == 0) {
            n += 2;
            x = x >> 2;
        }
        return n - (x & 1);
#endif
    }

    std::atomic<N *> *getChildPos(const uint8_t k);

  public:
    N16(uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : N(NTypes::N16, level, prefix, prefixLength) {
        memset(keys, 0, sizeof(keys));
        memset(children, 0, sizeof(children));
    }

    N16(uint32_t level, const Prefix &prefi) : N(NTypes::N16, level, prefi) {
        memset(keys, 0, sizeof(keys));
        memset(children, 0, sizeof(children));
    }

    inline bool insert(uint8_t key, N *n, bool flush);

    template <class NODE> void copyTo(NODE *n) const;

    void change(uint8_t key, N *val);

    std::atomic<N *> *getChild(const uint8_t k);

    bool remove(uint8_t k, bool force, bool flush);

    N *getAnyChild() const;

    void deleteChildren();

    void getChildren(uint8_t start, uint8_t end,
                     std::tuple<uint8_t, std::atomic<N *> *> children[],
                     uint32_t &childrenCount) ;

    uint32_t getCount() const;
};

class N48 : public N {
  public:
    std::atomic<uint8_t> childIndex[256];
    std::atomic<N *> children[48];

  public:
    static const uint8_t emptyMarker = 48;

    N48(uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : N(NTypes::N48, level, prefix, prefixLength) {
        memset(childIndex, emptyMarker, sizeof(childIndex));
        memset(children, 0, sizeof(children));
    }

    N48(uint32_t level, const Prefix &prefi) : N(NTypes::N48, level, prefi) {
        memset(childIndex, emptyMarker, sizeof(childIndex));
        memset(children, 0, sizeof(children));
    }

    inline bool insert(uint8_t key, N *n, bool flush);

    template <class NODE> void copyTo(NODE *n) const;

    void change(uint8_t key, N *val);

    std::atomic<N *> *getChild(const uint8_t k);

    bool remove(uint8_t k, bool force, bool flush);

    N *getAnyChild() const;

    void deleteChildren();

    void getChildren(uint8_t start, uint8_t end,
                     std::tuple<uint8_t, std::atomic<N *> *> children[],
                     uint32_t &childrenCount) ;

    uint32_t getCount() const;
};

class N256 : public N {
  public:
    std::atomic<N *> children[256];

  public:
    N256(uint32_t level, const uint8_t *prefix, uint32_t prefixLength)
        : N(NTypes::N256, level, prefix, prefixLength) {
        memset(children, '\0', sizeof(children));
    }

    N256(uint32_t level, const Prefix &prefi) : N(NTypes::N256, level, prefi) {
        memset(children, '\0', sizeof(children));
    }

    inline bool insert(uint8_t key, N *val, bool flush);

    template <class NODE> void copyTo(NODE *n) const;

    void change(uint8_t key, N *n);

    std::atomic<N *> *getChild(const uint8_t k);

    bool remove(uint8_t k, bool force, bool flush);

    N *getAnyChild() const;

    void deleteChildren();

    void getChildren(uint8_t start, uint8_t end,
                     std::tuple<uint8_t, std::atomic<N *> *> children[],
                     uint32_t &childrenCount);

    uint32_t getCount() const;
};
} // namespace PART_ns
#endif // ART_ROWEX_N_H
