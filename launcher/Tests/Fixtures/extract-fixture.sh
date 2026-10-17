#!/bin/sh
# Deterministic extractor for native UI tests, bundled only by the test project.
/bin/sleep 2
printf 'TLG_BYTES 0 1000\n'
/bin/sleep 1
printf 'TLG_BY'
/bin/sleep 0.1
printf 'TES 250 1000\n'
/bin/sleep 3
if [ "${TLG_TEST_EXTRACT_FAIL:-}" = 1 ]; then
    echo 'Extraction failed: fixture disk error'
    exit 1
fi
printf 'TLG_BYTES 750 1000\n'
/bin/sleep 3
/bin/mkdir -p "$2/CUSA03745/sce_sys"
/usr/bin/touch "$2/CUSA03745/eboot.bin" "$2/CUSA03745/sce_sys/param.sfo"
printf 'TLG_BYTES 1000 1000\n'
/bin/sleep 1
printf 'Extraction complete.\n'
