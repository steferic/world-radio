@echo off
idf.py -B build\n8r2 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.n8r2" -DSDKCONFIG="build\n8r2\sdkconfig" %*