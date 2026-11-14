#ifndef NINMAGIC_GUARDIAN_RECOVERY_H
#define NINMAGIC_GUARDIAN_RECOVERY_H
#include "guardian_addons.h"
#include "guardian_core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  gc_result result;
  unsigned addons_changed;
  int addons_quarantined;
} gr_result;
/* Shared integrated recovery. The caller must show and confirm the exact
 * ga_review (or a subset) before passing it. A pending already-confirmed plan
 * resumes unchanged. NULL explicitly requests files only: it never isolates
 * anything and must be explained as such in the UI when inventory is missing.
 * Validates the whole backup/SD/pending journal before any quarantine, then
 * performs durable reversible quarantine FIRST, and finally restores
 * environment files. The whole switch folder remains excluded. A failure
 * preserves journals/copies and explains whether add-ons remain quarantined.
 * Never performs general cleanup of new files or game data. */
gr_result gr_restore(gc_context *, const ga_review *confirmed_or_NULL);
/* permanent=1 must be an additional explicit irreversible confirmation.
 * Quarantine first, restore successfully, then discard its owned contents.
 * A durable discard intent can only resume the same COMPLETE displayed plan,
 * never a subset. NULL+permanent is rejected. If deletion has started, both
 * files-only and reversible mode are blocked until that plan completes. */
gr_result gr_restore_mode(gc_context *, const ga_review *confirmed_or_NULL,
                          int permanent);
#ifdef __cplusplus
}
#endif
#endif
