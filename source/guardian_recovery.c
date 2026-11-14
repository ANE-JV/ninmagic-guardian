#include "guardian_recovery.h"
#include <stdio.h>
#include <string.h>

#define GR_PLAN "config/NinMagicGuardian/quarantine/active/plan.txt"
#define GR_INTENT "config/NinMagicGuardian/quarantine/active/discard.txt"
static int active_plan(gc_context *c) {
  gc_stat st;
  int found;
  if (!c || !c->fs.stat)
    return -1;
  found = c->fs.stat(c->fs.user, GR_PLAN, &st);
  if (found < 0 || (found == 1 && st.type != GC_FILE))
    return -1;
  if (!found && c->fs.stat(c->fs.user, GR_INTENT, &st) != 0)
    return -1;
  return found == 1;
}
static int complete_plan(const ga_review *shown, const ga_review *plan) {
  unsigned i, j;
  unsigned char matched[GA_MAX_CANDIDATES] = {0};
  if (!shown || !shown->ok || !plan->ok || !plan->quarantine_pending ||
      shown->count != plan->count || shown->count > GA_MAX_CANDIDATES ||
      !shown->count || strcmp(shown->backup_sha256, plan->backup_sha256))
    return 0;
  for (i = 0; i < shown->count; i++) {
    for (j = 0; j < plan->count; j++)
      if (!matched[j] &&
          shown->candidates[i].kind == plan->candidates[j].kind &&
          !strcmp(shown->candidates[i].title_id,
                  plan->candidates[j].title_id) &&
          !memcmp(shown->candidates[i].fingerprint,
                  plan->candidates[j].fingerprint, 32))
        break;
    if (j == plan->count)
      return 0;
    matched[j] = 1;
  }
  return 1;
}
gr_result gr_restore_mode(gc_context *c, const ga_review *confirmed,
                          int permanent) {
  gr_result out;
  char detail[512];
  int pending, discarding = 0;
  ga_review plan;
  memset(&out, 0, sizeof out);
  out.result = gc_restore_preflight(c);
  if (!out.result.ok) {
    pending = active_plan(c);
    out.addons_quarantined = pending != 0;
    if (pending > 0) {
      snprintf(detail, sizeof detail, "%s", out.result.message);
      out.result.recovery_required = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "Hay un plan de extras guardado que se conserva. No termino la "
               "comprobacion para recuperar archivos. %.310s Revisa o continua "
               "el plan antes de reintentar.",
               detail);
    }
    return out;
  }
  pending = active_plan(c);
  if (pending < 0) {
    out.result.ok = 0;
    out.result.recovery_required = 1;
    out.addons_quarantined = 1;
    snprintf(
        out.result.message, sizeof out.result.message,
        "No se pudo comprobar el plan de aislamiento. Puede haber extras "
        "apartados; no se restauraron archivos ni se elimino ningun diario.");
    return out;
  }
  memset(&plan, 0, sizeof plan);
  if (permanent && (!confirmed || !confirmed->ok || !confirmed->count)) {
    out.result.ok = 0;
    out.result.recovery_required = pending;
    out.addons_quarantined = pending;
    snprintf(out.result.message, sizeof out.result.message,
             "La eliminacion definitiva requiere revisar y confirmar una "
             "seleccion completa. No se modifico ningun archivo.");
    return out;
  }
  if (pending) {
    if (ga_pending_review(c, &plan) || !plan.ok) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      out.addons_quarantined = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "No se pudo validar el plan de extras; no se modificaron "
               "archivos. %.380s",
               plan.message);
      return out;
    }
    discarding = plan.discard_pending;
    if ((discarding && !permanent) ||
        (permanent && !complete_plan(confirmed, &plan))) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      out.addons_quarantined = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "La eliminacion confirmada solo puede continuar con el mismo "
               "plan COMPLETO mostrado. No se puede reactivar, ampliar ni "
               "eliminar solo un subconjunto de ese plan. No se restauraron "
               "archivos.");
      return out;
    }
  }
  if (confirmed && !discarding) {
    ga_result addons;
    if (pending && ga_pending_covers(c, confirmed) != 1) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "Hay un plan de aislamiento pendiente y la revision no coincide "
               "con el. Resuelve ese plan desde Extras y revisa de nuevo antes "
               "de combinar otra seleccion. No se han restaurado archivos.");
      return out;
    }
    addons = ga_quarantine(c, confirmed);
    out.addons_changed = addons.changed;
    pending = active_plan(c);
    out.addons_quarantined = pending != 0;
    if (!addons.ok) {
      out.result.ok = 0;
      out.result.recovery_required =
          addons.recovery_required || out.addons_quarantined;
      snprintf(out.result.message, sizeof out.result.message,
               "No se restauraron archivos porque no termino el aislamiento "
               "confirmado. %.310s%s",
               addons.message,
               out.addons_quarantined
                   ? " Puede haber extras apartados; conserva el plan y "
                     "reintenta recuperar."
                   : "");
      return out;
    }
    if (pending < 0) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "El aislamiento se realizo, pero no se pudo comprobar su "
               "diario. Se conservan los extras apartados y no se restauraron "
               "archivos; reintenta recuperar.");
      return out;
    }
  } else
    out.addons_quarantined = pending;
  if (permanent && !discarding) {
    if (ga_pending_review(c, &plan) || !complete_plan(confirmed, &plan)) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      out.addons_quarantined = 1;
      snprintf(
          out.result.message, sizeof out.result.message,
          "El plan apartado no coincide con toda la seleccion confirmada. "
          "Se conserva reversible; no se restauraron ni eliminaron archivos.");
      return out;
    }
  }
  out.result = gc_restore(c);
  if (out.result.ok && permanent) {
    ga_result removed = ga_discard_quarantine(c);
    out.addons_changed = removed.changed;
    pending = active_plan(c);
    out.addons_quarantined = pending != 0;
    if (!removed.ok) {
      out.result.ok = 0;
      out.result.recovery_required = 1;
      snprintf(out.result.message, sizeof out.result.message,
               "Los archivos del sistema se restauraron y verificaron, pero "
               "no termino la eliminacion confirmada. %.290s Conserva los "
               "diarios y continua el mismo plan; no intentes reactivarlo.",
               removed.message);
      return out;
    }
    snprintf(out.result.message, sizeof out.result.message,
             "Archivos recuperados y verificados. Se elimino definitivamente "
             "solo el contenido apartado del plan confirmado; no queda copia "
             "para reactivarlo. Juegos, partidas, switch y archivos nuevos "
             "ajenos al plan se conservaron. Guardian actual se conserva.");
    return out;
  }
  if (!out.result.ok && out.addons_quarantined) {
    snprintf(detail, sizeof detail, "%s", out.result.message);
    out.result.recovery_required = 1;
    snprintf(
        out.result.message, sizeof out.result.message,
        "%s La restauracion de archivos no termino. %.300s Reintenta "
        "recuperar conservando el mismo plan.",
        discarding
            ? "La eliminacion ya estaba confirmada y puede estar "
              "parcialmente hecha; no se puede reactivar."
            : "Los extras seleccionados siguen apartados de forma reversible.",
        detail);
  } else if (out.result.ok && !confirmed && out.addons_quarantined) {
    snprintf(
        out.result.message, sizeof out.result.message,
        "Archivos recuperados y verificados. Se conserva el plan de extras "
        "anterior; esta eleccion de solo archivos no lo continuo ni lo cambio. "
        "Revisalo o reanudalo desde Extras. Guardian actual se conserva.");
  } else if (out.result.ok && out.addons_quarantined) {
    snprintf(out.result.message, sizeof out.result.message,
             "Archivos recuperados y verificados. El plan de extras confirmado "
             "sigue apartado y se puede reactivar desde Extras. Los juegos, "
             "partidas y archivos nuevos ajenos al respaldo se han conservado. "
             "Guardian actual se conserva.");
  } else if (out.result.ok && !confirmed) {
    snprintf(
        out.result.message, sizeof out.result.message,
        "Archivos recuperados y verificados. Se restauro solo la lista del "
        "respaldo; no se apartaron complementos nuevos. Juegos, partidas y "
        "archivos nuevos ajenos al respaldo se han conservado. Guardian actual "
        "se conserva.");
  }
  return out;
}

gr_result gr_restore(gc_context *c, const ga_review *confirmed) {
  return gr_restore_mode(c, confirmed, 0);
}
