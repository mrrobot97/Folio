/* SPDX-License-Identifier: Apache-2.0 */
#include "folio_metrics.h"
#include <assert.h>
#include <stdio.h>

static int calls;
int side_effect(void) { return ++calls; }

int main(void) {
    folio_metrics_init();
    folio_metrics_set_scenario(side_effect());
    folio_metrics_input(side_effect(), side_effect());
    folio_metrics_span_t span = folio_metrics_begin();
    folio_metrics_render_end(span);
    folio_metrics_display_end(span, side_effect(), side_effect(), side_effect(),
        side_effect(), side_effect(), side_effect(), side_effect());
    folio_metrics_input_end();
    assert(calls == 0);
    puts("metrics disabled: no argument side effects or runtime dependencies PASS");
    return 0;
}
