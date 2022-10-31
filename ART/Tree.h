#pragma once
#include "N.h"
#include "N16.h"
#include "N256.h"
#include "N4.h"
#include "N48.h"
#include "LeafArray.h"
#include <libpmemobj.h>
#include <set>

#include "global_log.h"
#include "../../../lib/latency_counter.h"

namespace PART_ns {
  using KVPair = std::pair<std::string, std::string>;

  extern uint64_t alloc_pm_size;
  inline void print_alloc_pm() {printf("Allocated %.2f MB PM space\n", alloc_pm_size / 1024.0 / 1024.0);}

class Tree {
  public:
  private:
    N *root;

    bool checkKey(const Key *ret, const Key *k) const;

  public:
    enum class CheckPrefixResult : uint8_t { Match, NoMatch, OptimisticMatch };

    enum class CheckPrefixPessimisticResult : uint8_t {
        Match,
        NoMatch,
        SkippedLevel
    };

    enum class PCCompareResults : uint8_t {
        Smaller,
        Equal,
        Bigger,
        SkippedLevel
    };
    enum class PCEqualsResults : uint8_t {
        BothMatch,
        Contained,
        NoMatch,
        SkippedLevel
    };
    enum class OperationResults : uint8_t {
        Success,
        NotFound, // remove
        Existed,  // insert
        UnSuccess
    };
    static CheckPrefixResult checkPrefix(N *n, const Key *k, uint32_t &level);

    static CheckPrefixPessimisticResult
    checkPrefixPessimistic(N *n, const Key *k, uint32_t &level,
                           uint8_t &nonMatchingKey, Prefix &nonMatchingPrefix);

    static PCCompareResults checkPrefixCompare(const N *n, const Key *k,
                                               uint32_t &level);

    static PCEqualsResults checkPrefixEquals(const N *n, uint32_t &level,
                                             const Key *start, const Key *end);

  public:
    Tree();

    Tree(const Tree &) = delete;

    ~Tree();

    void rebuild(std::vector<std::pair<uint64_t, size_t>> &rs,
                 uint64_t start_addr, uint64_t end_addr, int thread_id);


    Leaf *lookup(const Key *k) const;


    OperationResults update(const Key *k) const;

    bool prefixScan(const Key *prefix, const Key * continueKey, Leaf* result[], std::size_t resultSize, std::size_t &resultsFound) const ;

	
    // bool lookupRange(const Key *start, const Key *end, const Key *continueKey,
    //                  Leaf *result[], std::size_t resultLen,
    //                  std::size_t &resultCount) const;

    bool lookupRange(const Key *start, const Key *end, const Key *continueKey,
                       std::vector<KVPair>& result, std::size_t resultSize,
                       std::size_t &resultsFound) const;

    OperationResults insert(const Key *k);

    OperationResults remove(const Key *k);

    Leaf *allocLeaf(const Key *k) const;

    void graphviz_debug();

    /* add for zyw test */
    bool Put(const std::string& key, const std::string& value) {
      Key k;
      START
      std::string v(roart_pmlog::GenerateRawEntry(value));
      roart_pmlog::PmAddr addr = roart_pmlog::global_log_->Alloc(v.size());
      roart_pmlog::global_log_->Append(addr, v);
      LOG_END
      START
      //k.Init((char*)key.c_str(), key.size(), (char*)value.data(), value.size());
      k.Init((char*)key.c_str(), key.size(), (char*)(&addr), sizeof(uint64_t));
      OperationResults res = this->insert(&k);
      INDEX_END
      // printf("test\n");
      if (res == OperationResults::Success) {
        return true;
      } else {
        return false;
      }
    }

    bool Get(const std::string& key, std::string* value) {
      // std::string raw_value;
      Key k;
      uint64_t v = 0;
      START
      k.Init((char*)key.c_str(), key.size(), (char*)&v, 8);
      Leaf* ret = this->lookup(&k);
      INDEX_END
      if (ret == nullptr) {
        value = NULL;
        return false;
      }
      //std::string* temp = new std::string(ret->GetValue());
      //value = temp;
      START
      uint64_t addr = *(uint64_t*)(ret->GetValue());
      uint64_t value_size = roart_pmlog::DecodeSize(roart_pmlog::global_log_->raw() + addr);
      value->append(std::string(roart_pmlog::global_log_->raw() + addr + sizeof(uint64_t), value_size));
      LOG_END
      return true;
    }

    bool Delete(const std::string& key) {
      Key k;
      uint64_t value = 0;
      START
      k.Init((char*)key.c_str(), key.size(), (char*)&value, 8);
      OperationResults res = this->remove(&k);
      INDEX_END
      if (OperationResults::Success == res) {
        return true;
      } else {
        // not found
        return false;
      }
    }

    // prefix
    bool Scan(const std::string& key, std::vector<KVPair>& values) {
      PART_ns::Key k, maxkey;
      uint64_t value = 0;
      uint64_t prefix_len = 8;
      std::string prefix = key.substr(0, prefix_len);
      k.Init((char*)prefix.c_str(), key.size(), (char*)&value, 8);
      /* need to modify */
      std::string bigger = prefix;
      bigger[prefix_len-1] = bigger[prefix_len-1]+1;
      maxkey.Init((char*)bigger.data(), bigger.size(), (char*)&value, 8);

      size_t resultFound = 0;
      PART_ns::Key* continueKey = nullptr;
      START
      this->lookupRange(&k, &maxkey, continueKey, values, 0, resultFound);
      INDEX_END
      // printf("prefix = %s\n", key.substr(0, 18).c_str());
      // for (std::vector<KVPair>::const_iterator iter = values.cbegin(); iter != values.cend(); iter++) {
      //   printf("prefix scan: %s\n", (*iter).first.c_str());
      // }
      return true;
    }

    // range
    bool Scan(const std::string& key, int range, std::vector<KVPair>& values) {
      if(range <= 0) return false;
      PART_ns::Key k, maxkey;
      uint64_t value = 0;
      k.Init((char*)key.c_str(), key.size(), (char*)&value, 8);  
      /* need to modify */
      maxkey.Init((char*)"z", 1, (char*)&value, 8);

      size_t resultFound = 0;
      PART_ns::Key* continueKey = nullptr;
      std::size_t record_count = range;
      this->lookupRange(&k, &maxkey, continueKey, values, record_count, resultFound);
      return true;
    }

} __attribute__((aligned(64)));

#ifdef ARTPMDK
void *allocate_size(size_t size);

#endif

#ifdef COUNT_ALLOC
double getalloctime();
#endif

#ifdef CHECK_COUNT
int get_count();
#endif
} // namespace PART_ns
