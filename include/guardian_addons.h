#ifndef NINMAGIC_GUARDIAN_ADDONS_H
#define NINMAGIC_GUARDIAN_ADDONS_H
#include "guardian_core.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GA_MAX_CANDIDATES 32
typedef enum { GA_BOOT_MODULE = 1, GA_HOME_THEME = 2 } ga_kind;
typedef struct {
  char title_id[17];
  ga_kind kind;
  int newly_added;
  unsigned char fingerprint[32];
} ga_candidate;
typedef struct {
  int ok, quarantine_pending;
  unsigned count;
  char message[512];
  char backup_sha256[65];
  ga_candidate candidates[GA_MAX_CANDIDATES];
} ga_review;
typedef struct {
  int ok, recovery_required;
  unsigned changed;
  char message[512];
} ga_result;
/* Store hashes/presence only, after a successful explicitly requested backup.
 */
ga_result ga_capture(gc_context *);
/* Read-only comparison with inventory linked to the current backup. Results
 * mean changed/new boot add-ons, never proof that they caused a boot failure.
 */
ga_review ga_inspect(gc_context *);
/* Call only after the user confirms the displayed review. Rechecks fingerprints
 * and moves only boot2.flag or the fixed HOME theme's romfs. No game mods move.
 * If a previous confirmed operation was interrupted, resumes that same plan. */
ga_result ga_quarantine(gc_context *, const ga_review *);
/* Re-enable the exact quarantined entries; no existing destination is replaced.
 */
ga_result ga_restore_quarantine(gc_context *);
#ifdef __cplusplus
}
#endif
#endif
