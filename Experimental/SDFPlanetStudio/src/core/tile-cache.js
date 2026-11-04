/**
 * Byte-budgeted LRU cache for decoded pages.
 *
 * The point of a byte budget rather than an entry count: one page of 128 KiB SDF
 * cuts is either worth its footprint or not, and RAM has to survive a planet of
 * them. Eviction is pure LRU — streaming camera motion makes recency an excellent
 * predictor, and anything cleverer buys nothing at these sizes.
 */

export class TileCache {
  constructor({ budgetBytes = 64 * 1024 * 1024 } = {}) {
    this.budgetBytes = budgetBytes;
    this.entries = new Map(); // insertion order == recency order (oldest first)
    this.bytes = 0;
    this.stats = { hits: 0, misses: 0, evictions: 0, inserts: 0, evictedBytes: 0 };
  }

  get size() {
    return this.entries.size;
  }

  has(key) {
    return this.entries.has(key);
  }

  /** @returns {*} value, or undefined on miss (and records the statistic). */
  get(key) {
    const entry = this.entries.get(key);
    if (!entry) {
      this.stats.misses++;
      return undefined;
    }
    this.stats.hits++;
    // Refresh recency: delete + reinsert moves the key to the newest slot.
    this.entries.delete(key);
    this.entries.set(key, entry);
    return entry.value;
  }

  peek(key) {
    return this.entries.get(key)?.value;
  }

  set(key, value, bytes) {
    if (bytes > this.budgetBytes) return false;
    const existing = this.entries.get(key);
    if (existing) {
      this.bytes -= existing.bytes;
      this.entries.delete(key);
    }
    this.entries.set(key, { value, bytes });
    this.bytes += bytes;
    this.stats.inserts++;
    this.evictToBudget();
    return true;
  }

  evictToBudget() {
    let evicted = 0;
    while (this.bytes > this.budgetBytes && this.entries.size > 0) {
      const oldestKey = this.entries.keys().next().value;
      const entry = this.entries.get(oldestKey);
      this.entries.delete(oldestKey);
      this.bytes -= entry.bytes;
      this.stats.evictions++;
      this.stats.evictedBytes += entry.bytes;
      evicted++;
    }
    return evicted;
  }

  /** Keys in eviction order, oldest first. Used by tests and diagnostics. */
  keysByRecency() {
    return [...this.entries.keys()];
  }

  clear() {
    this.entries.clear();
    this.bytes = 0;
  }

  setBudget(bytes) {
    this.budgetBytes = Math.max(0, bytes);
    this.evictToBudget();
  }

  get utilisation() {
    return this.budgetBytes === 0 ? 0 : this.bytes / this.budgetBytes;
  }
}
