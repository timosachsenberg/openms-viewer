# Performance validation

The interactive peak-map path keeps the OpenMS experiment in its compact native
representation and rasterizes only to the current widget resolution. View changes
are coalesced for 45 ms, stale render requests are discarded, and the full-run
minimap raster is cached. This avoids constructing a second tabular copy of every
peak, which is the main reason an additional Parquet/DuckDB cache is not used by
the C++ application.

## Reference large-file smoke test

The large fixture shipped with the sibling `pyopenms-viewer` checkout was tested
on 2026-07-10 using the release-like local build and Qt's offscreen platform:

```bash
/usr/bin/time -f 'elapsed=%e peak_rss_kb=%M exit=%x' \
  timeout 25s env QT_QPA_PLATFORM=offscreen \
  ./build/openms-viewer \
  ../pyopenms-viewer/tests/data/DIA_HeLa_50ng_5_6min.mzML
```

Measured data and result:

| Property | Result |
| --- | ---: |
| mzML size | 2,738,007,516 bytes |
| Spectra | 8,822 |
| Peaks | 127,106,015 |
| Open-and-summarize time reported by the application | 11,196 ms |
| Peak resident set for the loaded UI and initial renders | 2,701,428 KiB (2.58 GiB) |
| Swap use | 0 |

The 25-second timeout terminates the event loop after successful loading; its
exit status of 124 is therefore expected. Results vary with storage, CPU, OpenMS
build options, and input compression. The application log records the measured
load duration for every mzML file, making the check reproducible on other systems.

imzML uses OpenMS's on-disc imzML/IBD reader instead because imaging data has a
different access pattern and can be much larger than the selected pixel or ion
window.
