#!/bin/sh
set -e

rm -rf cppcheck_html

cppcheck \
    --enable=warning,style,performance,portability \
	--check-level=exhaustive \
    --std=c11 \
    --platform=unix64 \
    --suppress=missingIncludeSystem \
    --inline-suppr \
    -Iapp/include \
    -Ilib/include \
    -Imgnt/include \
    -Inet/include \
    -I. \
    -j"$(nproc)" \
    --xml --xml-version=2 \
    app/ lib/ mgnt/ net/ main.c \
    2>/tmp/cppcheck.xml

cppcheck-htmlreport \
    --file=/tmp/cppcheck.xml \
    --report-dir=cppcheck_html \
    --source-dir=.

rm -f /tmp/cppcheck.xml

echo "cppcheck report generated OK: cppcheck_html/index.html"