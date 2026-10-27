# 109_build_watermark — Build Watermark

Family: Delivery. Scope: `sim::World` only.

## Context

Distribution builds often carry durable provenance marks: a watermark string,
a per-buyer build identifier, and section names retained from the build
pipeline. Any one mark alone is easy to rebrand; the correlation signal is that
the three representations survive independently across builds.

## Lab

Red plants all three representations on the lab World:

- watermark text (representation A),
- a per-build build id off the shared baseline (representation B),
- shared sections carrying a provenance marker, retained twice under the same
  name (representation C).

Blue correlates the three representations. Repeated section names count at
most once, so re-using a name cannot inflate the count into an extra
representation: two representations detect, all three mitigate, and a single
representation — repeated or not — stays undetected.

## Takeaway

Correlation robustness comes from counting distinct representations, not raw
artifact occurrences. Repetition inside one representation is noise, and
treating it as new evidence produces false positives.
