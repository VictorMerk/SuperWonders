#include "nvn/pipeline_cache_progress.h"
#include <cstdlib>

void check(bool ok) {
    if (!ok) std::abort();
}

int main() {
    nx::nvn::PipelineCacheProgress progress;
    check(!progress.dirty() && !progress.periodic_due());
    progress.changed();
    check(progress.dirty() && progress.periodic_due());
    const auto first = progress.revision();
    progress.not_grown(first);
    check(progress.dirty() && !progress.periodic_due());
    progress.failed();
    check(progress.periodic_due());
    progress.not_grown(first);
    progress.changed();
    progress.saved(first);
    check(progress.dirty() && progress.periodic_due());
    progress.saved(progress.revision());
    check(!progress.dirty() && !progress.periodic_due());
}
