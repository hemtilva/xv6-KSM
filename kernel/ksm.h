#ifndef _KSM_H_
#define _KSM_H_

struct ksmstat {
  uint64 scanned;     // Total page mappings examined by the scanner
  uint64 merged;      // Cumulative duplicate pages merged and freed
  uint64 shared;      // Current physical pages shared via KSM
  uint64 saved_pages; // Net physical frames currently saved
  uint64 cow_breaks;  // COW splits triggered by writes to merged pages
  uint64 scan_ticks;  // Total CPU timer ticks spent scanning
};

#endif
